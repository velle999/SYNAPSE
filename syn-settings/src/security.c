/* syn-settings — the Security pane.
 *
 * ⚠ IT OWNS ONE SWITCH AND NOTHING ELSE ABOUT synguard. The rules, the modes
 * and the AI's part are synguard's, in /etc/synguard; this pane reads the unit
 * and one file, and writes that file.
 *
 * The switch is kernel enforcement. synguard's unit arms its BPF-LSM gate by
 * default (synguard 0.1.0-44): the rules that lower to it are refused in the
 * kernel rather than killed after the fact. /etc/synguard/bpf-enforce saying
 * "off" is how a machine declines that — synguard reads it at start
 * (src/bpf_override.c there), so this pane writes it and restarts synguard.
 * The unit is never edited: a drop-in over ExecStart would shadow every later
 * change to the shipped line.
 *
 * ⛔ AND IT NEEDS ROOT, WITH NO SYSTEMD TOOL TO ASK. So it takes the path
 * boot.c and users.c take: the unprivileged half checks what it was given and
 * re-runs THIS BINARY under pkexec with --as-root. No polkit policy ships, so
 * pkexec asks for an administrator — which is the property worth keeping.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ⚠ SEAMS, for the reason scan.c's SYN_SETTINGS_SCAN_HOME is one: what this
 * pane says depends on a root-owned file and on how THIS machine booted, and a
 * test that read those would read the build box. tests/syn_settings_test.sh
 * points both at files it controls. */
static const char *override_path(void)
{
	return env_or("SYN_SETTINGS_GUARD_OVERRIDE", "/etc/synguard/bpf-enforce");
}

static const char *cmdline_path(void)
{
	return env_or("SYN_SETTINGS_CMDLINE", "/proc/cmdline");
}

/* Does the unit as systemd will run it ask for the gate at all? A synguard
 * older than 0.1.0-44, or an admin's drop-in over ExecStart, does not — and
 * then the file this pane writes changes nothing, so the switch must not
 * claim to be on. SYN_SETTINGS_GUARD_EXECSTART stands in for systemctl's
 * answer in the tests. */
static int unit_asks_for_gate(void)
{
	const char *seam = getenv("SYN_SETTINGS_GUARD_EXECSTART");
	if (seam) return strstr(seam, "--bpf-enforce") != NULL;
	if (!have_cmd("systemctl")) return 1;   /* cannot tell: trust the default */
	char out[1024] = "";
	char *a[] = { (char *)"systemctl", (char *)"show", (char *)"-p", (char *)"ExecStart",
	              (char *)"--value", (char *)"synguard.service", NULL };
	run_capture_quiet(a, out, sizeof out);
	if (!out[0]) return 1;
	return strstr(out, "--bpf-enforce") != NULL;
}

/* The same reading synguard makes (bpf_override.c): the first word that is not
 * a comment decides, and only "off" turns the gate off. Kept in step by hand —
 * it is one rule, and the test checks this side against the same cases. */
static int override_says_off(void)
{
	int fd = open(override_path(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return 0;
	char buf[256];
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n <= 0) return 0;
	buf[n] = '\0';
	for (char *line = buf; line && *line; ) {
		char *next = strchr(line, '\n');
		if (next) *next++ = '\0';
		line += strspn(line, " \t\r");
		if (*line && *line != '#') {
			size_t w = strcspn(line, " \t\r");
			return w == 3 && !strncmp(line, "off", 3);
		}
		line = next;
	}
	return 0;
}

/* synapse.bpf_enforce=0 on the kernel command line: the gate did not even
 * load this boot, whatever the switch says. */
static int booted_without_gate(void)
{
	char *c = slurp(cmdline_path());
	if (!c) return 0;
	int found = 0;
	for (char *t = strtok(c, " \t\n"); t && !found; t = strtok(NULL, " \t\n"))
		if (!strcmp(t, "synapse.bpf_enforce=0")) found = 1;
	free(c);
	return found;
}

static void unit_state(const char *unit, char *en, size_t en_cap,
                       char *act, size_t act_cap)
{
	char out[128] = "";
	char *is_en[]  = { (char *)"systemctl", (char *)"is-enabled", (char *)unit, NULL };
	char *is_act[] = { (char *)"systemctl", (char *)"is-active",  (char *)unit, NULL };

	run_capture_quiet(is_en, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	if (!strcmp(out, "not-found")) out[0] = '\0';
	snprintf(en, en_cap, "%s", out[0] ? out : "not installed");

	out[0] = '\0';
	run_capture_quiet(is_act, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(act, act_cap, "%s", out[0] ? out : "-");
}

int pane_security(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("synguard")) {
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Kernel enforcement"),
		        N_("needs synguard, the intrusion monitor"));
		return 0;
	}

	char en[64] = "unknown", act[64] = "-";
	if (have_cmd("systemctl"))
		unit_state("synguard.service", en, sizeof en, act, sizeof act);

	/* ⛔ A WHOLE SENTENCE PER BRANCH. The detail is drawn, and a clause
	 * spliced into a %s would ship English inside every language. */
	int off = override_says_off();
	const char *detail;
	if (!unit_asks_for_gate()) {
		rec_row("switch\t%s\toff\t%s\t%s\t-",
		        N_("Kernel enforcement"), act,
		        N_("synguard on this machine does not arm the kernel gate \xc2\xb7 it is older than 0.1.0-44, or its unit has been changed \xc2\xb7 its deny rules act only after the fact"));
		rec_row("unit\tsynguard.service\t%s\t%s\t%s\t-",
		        en, act, N_("the intrusion monitor"));
		return 0;
	}
	if (booted_without_gate())
		detail = off
		    ? N_("off, and this boot was also started with synapse.bpf_enforce=0, which keeps the gate from loading at all until the next normal boot")
		    : N_("on, but this boot was started with synapse.bpf_enforce=0, so the gate did not load \xc2\xb7 the next normal boot arms it");
	else if (off)
		detail = N_("off \xc2\xb7 synguard's deny rules still act, but only after the fact: the process is stopped once it has already done the thing \xc2\xb7 /etc/synguard/bpf-enforce says off");
	else
		detail = N_("the kernel refuses what synguard's deny rules forbid, before it happens, instead of stopping the process afterwards \xc2\xb7 on by default \xc2\xb7 turning it off asks for an administrator");

	rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:kernel-enforce",
	        N_("Kernel enforcement"), off ? "off" : "on", act, detail);

	rec_row("unit\tsynguard.service\t%s\t%s\t%s\t-",
	        en, act, N_("the intrusion monitor"));
	return 0;
}

/* ── The write ─────────────────────────────────────────────────────────── */

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* This binary, for the pkexec re-exec — see boot.c's self_path(). */
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

/* The root half: write or remove the file, then restart synguard so it reads
 * it. Removing is "on", not writing "on": absent is the shipped default, and a
 * file left behind saying "on" would be a second place the default is kept. */
static int apply_as_root(const char *val)
{
	const char *path = override_path();

	char *a[] = { (char *)"systemctl", (char *)"restart", (char *)"synguard.service", NULL };

	if (g_dry_run) {
		if (!strcmp(val, "off"))
			printf("would write %s: off\n", path);
		else
			printf("would remove %s\n", path);
		return run_or_show(a);
	}
	if (geteuid() != 0) return refuse("--as-root needs root");

	if (!strcmp(val, "off")) {
		ensure_parent(path);
		if (write_atomic(path, "# Written by Settings \xe2\x96\xb8 Security. "
		                       "Delete this file to arm the kernel gate again.\n"
		                       "off\n") != 0)
			return 1;
	} else if (unlink(path) != 0 && errno != ENOENT) {
		fprintf(stderr, "syn-settings: cannot remove %s: %s\n", path, strerror(errno));
		return 1;
	}
	return run_or_show(a);
}

/* `set kernel-enforce on|off` — the asking half. Returns -1 for a key that is
 * not this pane's, so set.c can go on looking. */
int security_set(const char *key, const char *val)
{
	if (strcmp(key, "kernel-enforce")) return -1;
	if (!val || (strcmp(val, "on") && strcmp(val, "off")))
		return refuse("kernel-enforce takes on or off");
	if (!have_cmd("synguard"))
		return refuse("synguard is not installed");

	char *a[] = { (char *)"pkexec", (char *)self_exe(), (char *)"security",
	              (char *)"kernel-enforce", (char *)val, (char *)"--as-root", NULL };
	return run_or_show(a);
}

/* `security kernel-enforce on|off --as-root` — what pkexec runs. */
int do_security(int argc, char **argv)
{
	int as_root = 0;
	for (int i = 0; i < argc; i++) if (!strcmp(argv[i], "--as-root")) as_root = 1;
	if (argc < 2 || strcmp(argv[0], "kernel-enforce"))
		return refuse("security takes: kernel-enforce on|off --as-root");
	if (strcmp(argv[1], "on") && strcmp(argv[1], "off"))
		return refuse("kernel-enforce takes on or off");
	if (!as_root)
		return refuse("--as-root is what pkexec runs; use `set kernel-enforce` instead");
	return apply_as_root(argv[1]);
}
