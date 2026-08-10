/* syn-settings — the writes.
 *
 * Every one of these hands the actual change to a systemd tool that does its
 * own polkit check. That is the whole security design and it is worth stating
 * plainly: this binary is NOT setuid, ships NO polkit policy of its own, and
 * adds no privilege. If localectl would have refused you at a terminal, it
 * refuses you here, for the same reason, with the same prompt.
 *
 * The alternative — a policy file granting "org.synapseos.settings.modify" and
 * a helper that writes /etc directly — is how a settings app becomes the most
 * interesting attack surface on the machine.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* A value that reaches a command line has to be checked here, because the
 * tools downstream are forgiving in ways that matter: a keymap name is not a
 * path and a timezone is not an option. execvp means no shell, so this is not
 * about quoting — it is about not passing "--something" as a positional
 * argument to a tool that would then read it as a flag. */
static int sane_value(const char *v)
{
	if (!v || !*v) return 0;
	if (v[0] == '-') return 0;
	for (const char *p = v; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') ||
		    *p == '_' || *p == '-' || *p == '.' || *p == '/' || *p == '+')
			continue;
		return 0;
	}
	return 1;
}

static int run_or_show(char *const argv[])
{
	if (g_dry_run) {
		fputs("would run:", stdout);
		for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
		putchar('\n');
		return 0;
	}
	int rc = run_quiet(argv);
	if (rc == -1) {
		fprintf(stderr, "syn-settings: could not run %s\n", argv[0]);
		return 1;
	}
	if (rc != 0)
		fprintf(stderr, "syn-settings: %s exited %d "
		                "(authorisation refused, or the value was rejected)\n",
		        argv[0], rc);
	return rc;
}

int do_set(int argc, char **argv)
{
	if (argc < 2) return refuse("set needs a key and a value");

	const char *key = argv[0];
	const char *val = argv[1];

	if (!sane_value(val))
		return refuse("value rejected: letters, digits, . _ - / + only, "
		              "and it may not begin with '-'");

	if (!strcmp(key, "keymap")) {
		char *a[] = { (char *)"localectl", (char *)"set-keymap",
		              (char *)val, NULL };
		return run_or_show(a);
	}
	if (!strcmp(key, "xkb")) {
		/* Variant is optional and separate: set-x11-keymap takes them as
		 * distinct arguments, and folding "us,intl" into one string is the
		 * mistake that produces a layout nobody asked for. */
		if (argc >= 3) {
			if (!sane_value(argv[2]))
				return refuse("variant rejected");
			char *a[] = { (char *)"localectl", (char *)"set-x11-keymap",
			              (char *)val, (char *)"", (char *)argv[2], NULL };
			return run_or_show(a);
		}
		char *a[] = { (char *)"localectl", (char *)"set-x11-keymap",
		              (char *)val, NULL };
		return run_or_show(a);
	}
	if (!strcmp(key, "locale")) {
		char *a[] = { (char *)"localectl", (char *)"set-locale",
		              (char *)val, NULL };
		return run_or_show(a);
	}
	if (!strcmp(key, "timezone")) {
		char *a[] = { (char *)"timedatectl", (char *)"set-timezone",
		              (char *)val, NULL };
		return run_or_show(a);
	}
	if (!strcmp(key, "ntp")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("ntp takes on or off");
		char *a[] = { (char *)"timedatectl", (char *)"set-ntp",
		              (char *)(strcmp(val, "on") ? "false" : "true"), NULL };
		return run_or_show(a);
	}

	return refuse("unknown key — try keymap, xkb, locale, timezone or ntp");
}

int do_unit(int argc, char **argv)
{
	if (argc < 2) return refuse("unit needs an action and a unit name");

	const char *act = argv[0];
	const char *unit = argv[1];

	if (strcmp(act, "enable") && strcmp(act, "disable") &&
	    strcmp(act, "start")  && strcmp(act, "stop") &&
	    strcmp(act, "restart"))
		return refuse("action must be enable, disable, start, stop or restart");

	if (!sane_value(unit) || !strchr(unit, '.'))
		return refuse("that does not look like a unit name");

	/* --no-block is deliberately NOT passed. A settings pane that reports
	 * success the instant it asked, rather than when the unit actually came
	 * up, is how "enabled" ends up meaning nothing — which is exactly the
	 * failure this app was built to make visible. */
	char *a[] = { (char *)"systemctl", (char *)act, (char *)unit, NULL };
	return run_or_show(a);
}
