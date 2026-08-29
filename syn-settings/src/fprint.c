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
#include "synsettings.h"

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
	{ "right-index-finger",  "Right index"  },
	{ "right-middle-finger", "Right middle" },
	{ "right-thumb",         "Right thumb"  },
	{ "right-ring-finger",   "Right ring"   },
	{ "right-little-finger", "Right little" },
	{ "left-index-finger",   "Left index"   },
	{ "left-middle-finger",  "Left middle"  },
	{ "left-thumb",          "Left thumb"   },
	{ "left-ring-finger",    "Left ring"    },
	{ "left-little-finger",  "Left little"  },
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
		rec_row("service\tfprintd\tnot installed\tbad\t"
		        "the fingerprint daemon is an optional dependency; without it "
		        "no reader can be used and the lock screen will not offer one\t"
		        "unavailable:fprintd");
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
		rec_row("device\treader\tnone found\t-\t"
		        "fprintd is installed but reports no fingerprint reader on "
		        "this machine\t-");
		return 0;
	}

	rec_row("device\treader\tpresent\tok\t"
	        "fprintd can see a reader; enrol a finger below and the lock "
	        "screen will offer it\t-");

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
		rec_row("enrolled\tfingerprints\tnone\twarn\t"
		        "the reader works, but there is nothing for it to match — the "
		        "lock screen stays on the password until a finger is enrolled\t-");
	else
		rec_row("enrolled\tfingerprints\t%d enrolled\tok\t"
		        "the lock screen offers the reader whenever one of these is on "
		        "file\tforget:all", have);

	for (int i = 0; i < NFINGERS; i++) {
		bool on = enrolled(listing, FINGERS[i].token);
		rec_row("finger\t%s\t%s\t%s\t%s\tenroll:%s",
		        FINGERS[i].label,
		        on ? "enrolled" : "not enrolled",
		        on ? "ok" : "-",
		        on ? "already on file; enrolling again replaces it"
		           : "enrol this finger — you will be asked to lift and rest "
		             "it on the reader several times",
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
