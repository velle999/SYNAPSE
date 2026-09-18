/*
 * fprint.c — the fingerprint reader: what is enrolled, and enrolling more.
 *
 * ⛔ THERE WAS NO WAY TO ENROL A FINGER ON THIS DESKTOP AT ALL. synui has had a
 * working lock-screen fingerprint path for a while (synui-lock-fprint.c), and
 * it answers "unavailable" both when there is no reader AND when the user has
 * enrolled nothing — which are indistinguishable from the outside. So on a
 * ThinkPad with a perfectly good reader the lock screen stayed silent, and the
 * only way to change that was `fprintd-enroll` in a terminal, if you knew it
 * existed. velle, 2026-08-28: "add fingerprint enroll gui".
 *
 * ⚠ THE OFFICIAL CLIs, NOT fprintd's D-Bus. fprintd-enroll/-delete/-list are
 * part of the fprintd package, are what every other desktop's documentation
 * points at, and handle the enrolment state machine — claim, stage, retry,
 * release — that this would otherwise be reimplementing over busctl. The cost
 * is that `fprintd-delete` takes a USER and removes everything: there is no
 * per-finger delete in the CLI, and inventing one out of raw D-Bus calls is
 * exactly the kind of cleverness that cannot be tested here. So "forget" is
 * all-or-nothing and says so.
 *
 * ⚠ NOTHING IN THIS FILE HAS RUN AGAINST A READER. The machine SynapseOS is
 * built on has no fingerprint hardware and no fprintd installed; velle's
 * ThinkPad has both. Every path that talks to a reader is therefore written
 * from fprintd's documented interface and tested only for what happens when it
 * is absent — which is the case this box can exercise, and the one every
 * desktop without a reader hits. The enrolment flow itself needs a swipe on
 * real hardware before it can be called verified.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synsettings.h"
#include "i18n.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * The finger names fprintd accepts, in the order a hand is usually offered.
 *
 * ⚠ THESE ARE fprintd's TOKENS, NOT LABELS. `fprintd-enroll -f` matches them
 * exactly, so the pretty name is a second column and never the argument — the
 * bug where a button reads "Right index finger" and runs `-f Right index
 * finger` is one substitution away in every design that keeps only one string.
 */
static const struct { const char *token, *label; } FINGERS[] = {
	{ "right-index-finger",  N_("Right index")  },
	{ "right-middle-finger", N_("Right middle") },
	{ "right-thumb",         N_("Right thumb")  },
	{ "right-ring-finger",   N_("Right ring")   },
	{ "right-little-finger", N_("Right little") },
	{ "left-index-finger",   N_("Left index")   },
	{ "left-middle-finger",  N_("Left middle")  },
	{ "left-thumb",          N_("Left thumb")   },
	{ "left-ring-finger",    N_("Left ring")    },
	{ "left-little-finger",  N_("Left little")  },
};
#define NFINGERS ((int)(sizeof(FINGERS) / sizeof(*FINGERS)))

static const char *fprint_user(void)
{
	const char *u = getenv("SUDO_USER");
	if (u && *u) return u;
	u = getenv("USER");
	if (u && *u) return u;
	return "";
}

/*
 * What `fprintd-list <user>` says, whole. Its shape is:
 *
 *   found 1 devices
 *   Device at /net/reactivated/Fprint/Device/0
 *   Using device /net/reactivated/Fprint/Device/0
 *   Fingerprints for user velle on Synaptics (press):
 *    - #0: right-index-finger
 *
 * ⚠ MATCHED BY TOKEN, NOT BY PARSING THE LIST. The line format has changed
 * across fprintd releases and carries a device name that can contain anything;
 * asking "does this output mention right-index-finger" is a question the
 * format cannot break, and a false positive would need a READER whose name
 * contains a finger token.
 */
static bool list_output(char *buf, size_t cap)
{
	buf[0] = '\0';
	if (!have_cmd("fprintd-list")) return false;
	const char *u = fprint_user();
	if (!*u) return false;
	char *argv[] = { (char *)"fprintd-list", (char *)u, NULL };
	run_capture(argv, buf, cap);
	return buf[0] != '\0';
}

static bool enrolled(const char *listing, const char *token)
{
	if (!listing || !*listing) return false;
	/* The token is bounded on the left by "- #N: " and on the right by a
	 * newline, so a plain substring search cannot match a longer finger name
	 * by accident — "right-index-finger" is not a prefix of any other. */
	return strstr(listing, token) != NULL;
}

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* ── sudo ───────────────────────────────────────────────────────────────── */

/*
 * The script that owns /etc/pam.d/sudo's fingerprint line. It answers `status`
 * without root — the stack is 0644 and the flag is a file — so the row reads
 * the same judgement the switch will act on, not a second copy of it here.
 */
static const char *sudo_fprint_bin(void)
{
	return env_or("SYN_SUDO_FPRINT_BIN", SYNSETTINGS_LIBDIR "/sudo-fprint");
}

/*
 * ⚠ THE VALUE IS WHAT sudo WILL DO, not what was asked. `on` means our line is
 * in sudo's stack now. A switch that read the flag would say "on" through the
 * window between asking and the next boot, and on a machine whose stack this
 * refuses to touch, forever.
 */
static void fprint_sudo_row(void)
{
	const char *bin = sudo_fprint_bin();
	if (access(bin, X_OK) != 0) return;

	char out[128] = "";
	char *argv[] = { (char *)bin, (char *)"status", NULL };
	if (run_capture_quiet(argv, out, sizeof out) != 0) return;
	out[strcspn(out, "\n")] = '\0';

	if (!strcmp(out, "on"))
		rec_row("sudo\t%s\t%s\tok\t%s\ttoggle:sudo-fingerprint",
		        N_("administrator prompts"), N_("on"),
		        N_("sudo in a terminal, and the password box that settings and software updates open, ask for a finger first; the password still works if you wait or no finger matches"));
	else if (!strcmp(out, "off"))
		rec_row("sudo\t%s\t%s\t-\t%s\ttoggle:sudo-fingerprint",
		        N_("administrator prompts"), N_("off"),
		        N_("sudo and the administrator password box ask for the password only; turn on to accept a finger first"));
	else if (!strcmp(out, "pending"))
		rec_row("sudo\t%s\t%s\twarn\t%s\ttoggle:sudo-fingerprint",
		        N_("administrator prompts"), N_("off"),
		        N_("switched on but not in every administrator prompt yet — the next boot applies it, or turn it on here"));
	else if (!strcmp(out, "stuck:foreign-line"))
		rec_row("sudo\t%s\t%s\t-\t%s\t-",
		        N_("administrator prompts"), N_("on"),
		        N_("a pam_fprintd line written by hand is in /etc/pam.d/sudo; this switch leaves it alone"));
	else if (!strcmp(out, "stuck:no-module"))
		rec_row("sudo\t%s\t%s\twarn\t%s\t-",
		        N_("administrator prompts"), N_("off"),
		        N_("pam_fprintd is not installed, so no administrator prompt can ask for a finger"));
	else if (!strcmp(out, "stuck:unexpected-stack"))
		rec_row("sudo\t%s\t%s\twarn\t%s\t-",
		        N_("administrator prompts"), N_("off"),
		        N_("/etc/pam.d/sudo is not the stack this expects, so it is left alone — add pam_fprintd.so as sufficient above its auth include"));
}

int fprint_set_sudo(const char *val)
{
	if (strcmp(val, "on") && strcmp(val, "off"))
		return refuse("sudo-fingerprint takes on or off");
	const char *bin = sudo_fprint_bin();
	if (access(bin, X_OK) != 0)
		return refuse("the sudo fingerprint script is missing — reinstall syn-settings");
	/* pkexec, as the boot pane and the firewall do: it is sudo's own PAM
	 * stack, so the bar is an administrator's password. No polkit policy of
	 * ours ships, which makes that pkexec's default. */
	char *a[] = { (char *)"pkexec", (char *)bin, (char *)val, NULL };
	return run_or_show(a);
}

int pane_fprint(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	/*
	 * ⛔ THE THREE WAYS THIS IS UNAVAILABLE ARE DIFFERENT ANSWERS, and the
	 * lock screen cannot tell them apart — pam_fprintd reports "no reader",
	 * "fprintd not installed" and "nothing enrolled" as one indistinguishable
	 * PAM_AUTHINFO_UNAVAIL. That is exactly why the reader looked like it
	 * "only works sometimes": there was nowhere that said which.
	 */
	if (!have_cmd("fprintd-enroll")) {
		rec_row("service\t%s\t%s\t%s\t%s\tunavailable:fprintd",
		        N_("fprintd"), N_("not installed"), N_("bad"),
		        N_("the fingerprint daemon is an optional dependency; without it no reader can be used and the lock screen will not offer one"));
		return 0;
	}

	char listing[4096];
	bool listed = list_output(listing, sizeof listing);

	/*
	 * A reader, or not. `fprintd-list` prints "found 0 devices" on a machine
	 * with fprintd installed and no hardware, which is most desktops.
	 */
	bool device = listed && strstr(listing, "found 0 devices") == NULL;
	if (!device) {
		rec_row("device\t%s\t%s\t-\t%s\t-",
		        N_("reader"), N_("none found"),
		        N_("fprintd is installed but reports no fingerprint reader on this machine"));
		return 0;
	}

	rec_row("device\t%s\t%s\tok\t%s\t-",
	        N_("reader"), N_("present"),
	        N_("fprintd can see a reader; enrol a finger below and the lock screen will offer it"));

	fprint_sudo_row();

	int have = 0;
	for (int i = 0; i < NFINGERS; i++)
		if (enrolled(listing, FINGERS[i].token)) have++;

	/*
	 * ⚠ THE COUNT IS ITS OWN ROW BECAUSE ZERO IS THE INTERESTING CASE. "No
	 * fingerprints enrolled" is the state that makes a working reader look
	 * broken at the lock screen, and it is invisible in a table of ten rows
	 * that all say the same thing.
	 */
	if (have == 0)
		rec_row("enrolled\t%s\t%s\t%s\t%s\t-",
		        N_("fingerprints"), N_("none"), N_("warn"),
		        N_("the reader works, but there is nothing for it to match — the lock screen stays on the password until a finger is enrolled"));
	else
		rec_row("enrolled\t%s\t%d enrolled\tok\t%s\tforget:all",
		        N_("fingerprints"), have,
		        N_("the lock screen offers the reader whenever one of these is on file"));

	/*
	 * ⛔ EVERY CELL A PERSON READS IS MARKED, AND THE RECORD STILL CARRIES
	 * ENGLISH. These ten rows only exist on a machine with a reader, so for
	 * two releases they were the only drawn labels in this program that were
	 * not msgids and no check on a reader-less box could see it: the drawn-
	 * label check reads the RECORD, and a record that is never emitted here
	 * says nothing. It failed on velle's ThinkPad, where the reader is
	 * enrolled, and took syn-update down with it.
	 *
	 * ⚠ AND `enrolled` STAYS ENGLISH IN THE ROW, which is the whole point of
	 * N_(). data/syn-settings.qml compares `root.selValue === "enrolled"` to
	 * decide whether the button says "Enrol again…", and it translates the
	 * cell only on its way to the screen.
	 */
	for (int i = 0; i < NFINGERS; i++) {
		bool on = enrolled(listing, FINGERS[i].token);
		rec_row("finger\t%s\t%s\t%s\t%s\tenroll:%s",
		        FINGERS[i].label,
		        on ? N_("enrolled") : N_("not enrolled"),
		        on ? "ok" : "-",
		        on ? N_("already on file; enrolling again replaces it")
		           : N_("enrol this finger — you will be asked to lift and rest "
		                "it on the reader several times"),
		        FINGERS[i].token);
	}

	return 0;
}

/* ── the writes ─────────────────────────────────────────────────────────── */

static bool known_finger(const char *token)
{
	for (int i = 0; i < NFINGERS; i++)
		if (!strcmp(FINGERS[i].token, token)) return true;
	return false;
}

/*
 * `syn-settings enroll <finger>` — the swipes, streamed.
 *
 * ⚠ run_progress(), NOT run_quiet(). Enrolment is the one write in this program
 * that TALKS while it works: fprintd-enroll prints a line per stage
 * ("Enroll result: enroll-stage-passed") and the whole interaction is those
 * lines. Run quietly it would be a button that does nothing visible for twenty
 * seconds and then either works or does not.
 */
int cmd_enroll(const char *finger)
{
	if (!finger || !*finger) {
		fprintf(stderr, "syn-settings: enroll: which finger?\n");
		return 2;
	}
	/* ⛔ CHECKED AGAINST THE TABLE, NOT PASSED THROUGH. This string arrives
	 * from the GUI and becomes an argument to a program; an allowlist of ten
	 * tokens is the whole validation and it costs nothing. */
	if (!known_finger(finger)) {
		fprintf(stderr, "syn-settings: enroll: '%s' is not a finger fprintd "
		                "knows\n", finger);
		return 2;
	}
	if (!have_cmd("fprintd-enroll")) {
		fprintf(stderr, "syn-settings: enroll: fprintd is not installed\n");
		return 1;
	}

	char *argv[] = { (char *)"fprintd-enroll", (char *)"-f",
	                 (char *)finger, NULL };
	return run_progress(argv) == 0 ? 0 : 1;
}

/*
 * `syn-settings forget all` — every print for this account.
 *
 * ⚠ ALL OF THEM, AND THE NAME SAYS SO. fprintd-delete takes a user and removes
 * the lot; there is no per-finger delete in the CLI. A `forget <finger>` that
 * quietly removed everything would be the worst possible shape, so the argument
 * is the word `all` and nothing else is accepted.
 */
int cmd_forget(const char *what)
{
	if (!what || strcmp(what, "all") != 0) {
		fprintf(stderr, "syn-settings: forget: only `forget all` is supported "
		                "— fprintd removes a user's prints together\n");
		return 2;
	}
	if (!have_cmd("fprintd-delete")) {
		fprintf(stderr, "syn-settings: forget: fprintd is not installed\n");
		return 1;
	}
	const char *u = fprint_user();
	if (!*u) {
		fprintf(stderr, "syn-settings: forget: cannot tell which account\n");
		return 1;
	}
	char *argv[] = { (char *)"fprintd-delete", (char *)u, NULL };
	return run_progress(argv) == 0 ? 0 : 1;
}

/* ── for the Users pane ─────────────────────────────────────────────────── */

bool fprint_known_finger(const char *token) { return known_finger(token); }

/* `choices finger/<user>`: the ten fingers, token and label. Nothing is
 * ticked — another account's prints are not readable without root, and a
 * tick on only the rows this account can see would read as the whole truth. */
int fprint_finger_choices(void)
{
	for (int i = 0; i < NFINGERS; i++)
		rec_row("%s\t%s\t-", FINGERS[i].token, FINGERS[i].label);
	return 0;
}

/* A reader fprintd can see — the condition for offering a finger at all. */
bool fprint_reader_present(void)
{
	if (!have_cmd("fprintd-enroll")) return false;
	char listing[4096];
	return list_output(listing, sizeof listing)
	    && strstr(listing, "found 0 devices") == NULL;
}
