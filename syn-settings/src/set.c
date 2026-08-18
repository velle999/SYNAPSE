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

int do_set(int argc, char **argv)
{
	/* Before the argument count and before the general value filter, because
	 * this one takes a role AND an application and validates the application
	 * where it is used — a .desktop name is checked by looking for the file,
	 * which is a stronger test than any character class. */
	if (argc >= 1 && !strcmp(argv[0], "app"))
		return do_set_app(argc - 1, argv + 1);

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
	/* The radios. Both go through the tool that owns the device and does its
	 * own polkit check — nmcli for wifi, bluetoothctl for the adapter — for
	 * the same reason localectl handles the keymap.
	 *
	 * Deliberately NOT rfkill: it needs root outright, so a button wired to it
	 * would fail for the user it exists to serve. rfkill's soft/hard block
	 * stays a READ on the Bluetooth pane, because a hard block is a physical
	 * switch and no amount of software can clear it — showing it is the point,
	 * offering to toggle it would be a lie. */
	/* The AI backend. Handed straight to synui-ai-backend(1), which owns
	 * every part of making this stick — off has to MASK synapd.socket and
	 * synapd.service, not stop them, and record the choice outside /run. Four
	 * separate resurrection paths were closed in that helper; a second
	 * implementation here would reopen all four.
	 *
	 * The helper self-elevates with `sudo -n` for the writes, so this stays a
	 * plain exec and the privilege question is answered in one place. */
	if (!strcmp(key, "ai-backend")) {
		if (strcmp(val, "gpu") && strcmp(val, "cpu") && strcmp(val, "off"))
			return refuse("ai-backend takes gpu, cpu or off");
		if (!have_cmd("synui-ai-backend"))
			return refuse("synui-ai-backend is not installed "
			              "(it ships with the synui package)");
		char *a[] = { (char *)"synui-ai-backend", (char *)val, NULL };
		return run_or_show(a);
	}

	if (!strcmp(key, "wifi")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("wifi takes on or off");
		char *a[] = { (char *)"nmcli", (char *)"radio", (char *)"wifi",
		              (char *)val, NULL };
		return run_or_show(a);
	}
	if (!strcmp(key, "bluetooth")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("bluetooth takes on or off");
		char *a[] = { (char *)"bluetoothctl", (char *)"power",
		              (char *)val, NULL };
		return run_or_show(a);
	}

	/* The desktop clock's own three. Not a systemd tool and not privileged —
	 * this is the user's file in the user's config directory — but it goes
	 * through `set` like everything else so there is one write verb. */
	if (!strcmp(key, "time-format") || !strcmp(key, "time-seconds") ||
	    !strcmp(key, "date-format"))
		return do_set_clock(key, val);

	if (!strcmp(key, "ntp")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("ntp takes on or off");
		char *a[] = { (char *)"timedatectl", (char *)"set-ntp",
		              (char *)(strcmp(val, "on") ? "false" : "true"), NULL };
		return run_or_show(a);
	}

	return refuse("unknown key — try keymap, xkb, locale, timezone, ntp, "
	              "time-format, time-seconds, date-format, wifi or bluetooth");
}

/* Bring a single interface up or down.
 *
 * This is what was missing: the pane had a Wi-Fi RADIO toggle and nothing at
 * all for the wired link, so on a desktop whose only connection is ethernet
 * the Network pane could not change one thing. `nmcli device` covers both —
 * a radio switch and a link are different controls and both belong here.
 */
int do_device(int argc, char **argv)
{
	if (argc < 2) return refuse("device needs connect|disconnect and a name");

	const char *act = argv[0], *dev = argv[1];

	if (strcmp(act, "connect") && strcmp(act, "disconnect"))
		return refuse("device action must be connect or disconnect");

	/* An interface name, not a path and not a flag. */
	if (!sane_value(dev)) return refuse("that does not look like an interface");

	/* Refusing loopback here rather than letting NetworkManager do it: taking
	 * lo down breaks everything that talks to itself, which on this machine is
	 * every daemon with a UNIX socket, and the error you would get back is
	 * about NetworkManager rather than about what you just broke. */
	if (!strcmp(dev, "lo")) return refuse("refusing to touch loopback");

	if (!have_cmd("nmcli"))
		return refuse("NetworkManager is not installed");

	char *a[] = { (char *)"nmcli", (char *)"device", (char *)act,
	              (char *)dev, NULL };
	return run_or_show(a);
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
