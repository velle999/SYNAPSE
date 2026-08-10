/* syn-settings — the Keyboard & Region pane.
 *
 * localectl and timedatectl already own these answers, including the parts
 * that are easy to get subtly wrong: the console keymap and the X11 layout are
 * SEPARATE settings that usually agree and sometimes do not, and the installer
 * has shipped a bug from exactly that confusion before
 * (project_synapse_installer_keymap_namespaces). Showing both, labelled, is
 * the point — a pane that printed one "keyboard layout" would hide the failure
 * it exists to surface.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <string.h>

/* The trailing `action` column is what makes this pane editable without the
 * GUI having to know anything about localectl.
 *
 * The alternative was a QML switch over key names, which would mean the list
 * of what can be changed lives in two places and the front-end silently offers
 * to set something the binary would refuse. Here the reader declares it: a row
 * that says "set:keymap" can be edited, one that says "-" is a fact you can
 * only read. A new editable setting is one C line and no QML at all. */
static void row_or_unknown(const char *key, const char *text,
                           const char *field, const char *detail,
                           const char *action)
{
	char val[256];
	if (!scrape_field(text, field, val, sizeof val) || !val[0])
		snprintf(val, sizeof val, "unknown");
	rec_row("%s\t%s\t%s\t%s", key, val, detail, action);
}

int pane_region(void)
{
	rec_header("key\tvalue\tdetail\taction");

	/* ── Keyboard ─────────────────────────────────────────────────────── */
	if (have_cmd("localectl")) {
		char out[4096] = "";
		char *argv[] = { (char *)"localectl", (char *)"status", NULL };
		run_capture(argv, out, sizeof out);

		row_or_unknown("locale", out, "System Locale",
		               "LANG and friends, /etc/locale.conf", "set:locale");
		row_or_unknown("keymap-console", out, "VC Keymap",
		               "the tty and the greeter", "set:keymap");
		/* localectl prints "X11 Layout" even on a Wayland-only system: it is
		 * the xkb layout, which is what synui actually loads. The name is
		 * historical, so it is relabelled here rather than repeated. */
		row_or_unknown("keymap-xkb", out, "X11 Layout",
		               "xkb layout — what the desktop uses", "set:xkb");
		row_or_unknown("keymap-xkb-variant", out, "X11 Variant",
		               "xkb variant, blank for the default", "-");
		row_or_unknown("keymap-xkb-model", out, "X11 Model",
		               "xkb model", "-");
	} else {
		rec_row("locale\tunknown\tlocalectl not installed\t-");
		rec_row("keymap-console\tunknown\tlocalectl not installed\t-");
		rec_row("keymap-xkb\tunknown\tlocalectl not installed\t-");
	}

	/* ── Time ─────────────────────────────────────────────────────────── */
	if (have_cmd("timedatectl")) {
		char out[4096] = "";
		char *argv[] = { (char *)"timedatectl", (char *)"status", NULL };
		run_capture(argv, out, sizeof out);

		row_or_unknown("timezone", out, "Time zone", "/etc/localtime", "set:timezone");
		row_or_unknown("clock-local", out, "Local time", "as the system reads it", "-");
		row_or_unknown("clock-utc", out, "Universal time", "UTC", "-");
		row_or_unknown("rtc", out, "RTC time", "the hardware clock", "-");
		/* Two different questions that read almost the same: whether the NTP
		 * CLIENT is running, and whether the clock has actually been
		 * disciplined by it. A machine can have the first without the second
		 * for a long time, and only the second means the clock is right.
		 * Lynis TIME-3104 asks about the first. */
		row_or_unknown("ntp-enabled", out, "NTP service",
		               "is a time client running", "toggle:ntp");
		row_or_unknown("ntp-synced", out, "System clock synchronized",
		               "has it actually disciplined the clock", "-");
	} else {
		rec_row("timezone\tunknown\ttimedatectl not installed\t-");
		rec_row("ntp-enabled\tunknown\ttimedatectl not installed\t-");
	}

	return 0;
}
