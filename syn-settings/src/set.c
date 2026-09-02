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
#include "i18n.h"

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

/* The names the assistant answers to.
 *
 * Written here rather than handed to a tool, because there is no verb for it:
 * vibe's matcher reads wake.state directly. ⚠ Refused EMPTY — a wake.state with
 * no words in it is an assistant that cannot be woken at all, and the file
 * being present is exactly what stops the matcher falling back to its built-in
 * pair. "Delete the file" is the way back, and the message says so.
 *
 * Its own validator, because this is the one value in this app that legally
 * contains commas and spaces. Letters only otherwise: a wake word is something
 * a person SAYS, so punctuation in it could never be matched against a
 * transcription anyway.
 */
static int set_wake_words(const char *val)
{
	int any = 0;
	for (const char *p = val; *p; p++) {
		if (*p == ',' || *p == ' ')
			continue;
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
			return refuse("a wake word is letters only, "
			              "separated by commas");
		any = 1;
	}
	if (!any)
		return refuse("wake-words needs at least one word "
		              "(delete ~/.config/synui/wake.state to go back to "
		              "the default)");

	char cfg[384], path[512], body[512];
	config_home(cfg, sizeof cfg);
	if (!cfg[0])
		return refuse("cannot find the config directory");
	snprintf(path, sizeof path, "%s/synui/wake.state", cfg);
	snprintf(body, sizeof body,
	         "# The names the assistant answers to. Comma separated.\n"
	         "words = %s\n", val);
	ensure_parent(path);
	if (write_atomic(path, body) != 0)
		return refuse("could not write wake.state");
	printf("wake words: %s\n", val);
	return 0;
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

	/* ⛔ BEFORE sane_value(), AND ONLY THIS KEY. The generic gate rejects a
	 * comma, because almost every value here is handed to an external command
	 * and a comma is one of the characters that guard exists for. `wake-words`
	 * is a COMMA-SEPARATED LIST that reaches no command at all — it is written
	 * to a file this function writes itself — so the gate refuses every legal
	 * input for it and the row advertises a control that cannot be used.
	 *
	 * That is the dead-button failure in a new disguise, found by trying the
	 * exact value the row's own help text suggests. Loosening sane_value()
	 * instead would weaken the guard for the thirty keys that DO exec.
	 */
	if (!strcmp(key, "wake-words"))
		return set_wake_words(val);

	if (!sane_value(val))
		return refuse("value rejected: letters, digits, . _ - / + only, "
		              "and it may not begin with '-'");

	if (!strcmp(key, "assistant-backend"))
		return assistant_set_backend(argc > 1 ? argv[1] : NULL);

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
	/* ── The machine's name ──────────────────────────────────────────────
	 *
	 * `hostnamectl` does its own polkit check, exactly as localectl and
	 * timedatectl do above — so this stays a settings app that runs a
	 * systemd tool rather than one that ships a way to become root.
	 *
	 * ⚠ VALIDATED HERE ANYWAY, and more narrowly than sane_value: a hostname
	 * is not a filename. Letters, digits, hyphen and dot, no leading or
	 * trailing hyphen or dot, 63 characters. systemd would reject a bad one
	 * too — but it would reject it with an exit code, and "hostnamectl exited
	 * 1" is not a sentence that tells anybody what was wrong with what they
	 * typed.
	 *
	 * ⚠ Worth doing at all because EVERY SynapseOS install is `synapse`: two
	 * on one network and Avahi renames one of them `synapse-2.local`, with no
	 * say in which, and the .local address stops being stable. */
	if (!strcmp(key, "hostname")) {
		size_t n = strlen(val);
		if (n > 63) return refuse("a hostname is at most 63 characters");
		if (val[0] == '-' || val[0] == '.' || val[n - 1] == '-' || val[n - 1] == '.')
			return refuse("a hostname may not begin or end with '-' or '.'");
		for (const char *p = val; *p; p++) {
			if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			    (*p >= '0' && *p <= '9') || *p == '-' || *p == '.')
				continue;
			return refuse("a hostname takes letters, digits, '-' and '.' only");
		}
		char *a[] = { (char *)"hostnamectl", (char *)"set-hostname",
		              (char *)val, NULL };
		return run_or_show(a);
	}

	/* The desktop's accent, on the RGB hardware. syn-rgb(1) owns the state,
	 * the systemd path unit and the hardware; this is the switch, and it is
	 * the SAME command the control panel's row runs — one owner, two doors.
	 *
	 * ⚠ No privilege anywhere: the lights are a user's own session, so this
	 * is the one write in this file that does not go through a tool with a
	 * polkit check because there is nothing to check. */
	if (!strcmp(key, "rgb")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("rgb takes on or off");
		char *a[] = { (char *)"syn-rgb", (char *)val, NULL };
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

	/* ── Speech ────────────────────────────────────────────────────────
	 *
	 * ⛔ EVERY ONE OF THESE HAS A ROW IN speech.c THAT NAMES IT. A pane that
	 * emits an action token with no verb here draws a control that does
	 * nothing when pressed — the dead-button failure this app has had before.
	 * If a row is added there, a case belongs here in the same commit.
	 *
	 * All of them go through the owning tool rather than writing the state
	 * file: `syn-speak on` also starts the announcer, `vibe wake on` also
	 * enables the user unit, and `syn-speak rate` speaks a sample so the
	 * number means something. A settings app that wrote the key and stopped
	 * would leave a desktop that is silent while its switch says On. */
	if (!strcmp(key, "screen-reader")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("screen-reader takes on or off");
		if (!have_cmd("syn-speak"))
			return refuse("syn-speak is not installed "
			              "(it ships with the synui package)");
		char *a[] = { (char *)"syn-speak", (char *)val, NULL };
		return run_or_show(a);
	}

	if (!strcmp(key, "wake-word")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("wake-word takes on or off");
		if (!have_cmd("vibe"))
			return refuse("vibe is not installed \xc2\xb7 synpkg install vibe");
		/* ⚠ `on` here OPENS A MICROPHONE and keeps it open. vibe prints what
		 * it has done; this does not add a second sentence, because the one
		 * place that should describe it is the one that owns it. */
		char *a[] = { (char *)"vibe", (char *)"wake", (char *)val, NULL };
		return run_or_show(a);
	}

	if (!strcmp(key, "speech-rate") || !strcmp(key, "speech-volume")) {
		char *end = NULL;
		long n = strtol(val, &end, 10);
		if (!*val || (end && *end))
			return refuse("that is not a number");
		int rate = !strcmp(key, "speech-rate");
		if (rate ? (n < 80 || n > 450) : (n < 0 || n > 100))
			return refuse(rate ? "speech-rate takes 80 to 450"
			                   : "speech-volume takes 0 to 100");
		if (!have_cmd("syn-speak"))
			return refuse("syn-speak is not installed "
			              "(it ships with the synui package)");
		char *a[] = { (char *)"syn-speak",
		              (char *)(rate ? "rate" : "volume"), (char *)val, NULL };
		return run_or_show(a);
	}

	/* The firewall.
	 *
	 * ⚠ THROUGH synnet, not by writing /etc/synnet/firewall here. Off is two
	 * operations that have to happen together — record the preference AND take
	 * the chain down — and synnet already does both in one place. Writing only
	 * the file from here would leave the chain up until the daemon next looked;
	 * writing only the chain would have the daemon put it back within the
	 * minute. A second implementation gets exactly one of those wrong.
	 *
	 * pkexec, as the boot pane does: loading an nftables chain needs root, and
	 * no polkit policy of our own ships — so pkexec demands admin
	 * authentication, which is the right bar for the one setting in this app
	 * that makes the machine less safe. */
	if (!strcmp(key, "firewall")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("firewall takes on or off");
		if (!have_cmd("synnet"))
			return refuse("synnet is not installed — it is what applies the "
			              "firewall on this system");
		char *a[] = { (char *)"pkexec", (char *)"synnet",
		              (char *)"--firewall", (char *)val, NULL };
		return run_or_show(a);
	}

	if (!strcmp(key, N_("wifi"))) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("wifi takes on or off");
		char *a[] = { (char *)"nmcli", (char *)"radio", (char *)N_("wifi"),
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

	/* The loopback port that lets llama.cpp-shaped frontends reach synapd.
	 *
	 * ⛔ ONE SWITCH, enable AND start. The generic `unit` verb can do this in
	 * two steps, and two steps is how somebody ends up with a socket that is
	 * enabled and not listening — or listening now and gone after a reboot.
	 * Neither state is one anybody asked for.
	 *
	 * ⚠ THE SOCKET, NOT THE SERVICE. systemd starts the proxy on the first
	 * connection; enabling the service instead gets a proxy for a port nothing
	 * is listening on. */
	if (!strcmp(key, "llama-api")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("llama-api takes on or off");
		char *a[] = { (char *)"systemctl",
		              (char *)(strcmp(val, "on") ? "disable" : "enable"),
		              (char *)"--now",
		              (char *)"synapd-http-proxy.socket", NULL };
		return run_or_show(a);
	}

	if (!strcmp(key, "ntp")) {
		if (strcmp(val, "on") && strcmp(val, "off"))
			return refuse("ntp takes on or off");
		char *a[] = { (char *)"timedatectl", (char *)"set-ntp",
		              (char *)(strcmp(val, "on") ? "false" : "true"), NULL };
		return run_or_show(a);
	}

	return refuse("unknown key — try keymap, xkb, locale, timezone, ntp, "
	              "time-format, time-seconds, date-format, wifi, bluetooth "
	              "or llama-api");
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
