/* syn-settings — the Keyboard & Language pane.
 *
 * localectl already owns these answers, including the parts
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
#include "i18n.h"

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
		               N_("LANG and friends, /etc/locale.conf"), "set:locale");
		row_or_unknown("keymap-console", out, "VC Keymap",
		               N_("the tty and the greeter"), "set:keymap");
		/* localectl prints "X11 Layout" even on a Wayland-only system: it is
		 * the xkb layout, which is what synui actually loads. The name is
		 * historical, so it is relabelled here rather than repeated. */
		row_or_unknown("keymap-xkb", out, "X11 Layout",
		               N_("xkb layout — what the desktop uses"), "set:xkb");
		row_or_unknown(N_("keymap-xkb-variant"), out, "X11 Variant",
		               N_("xkb variant, blank for the default"), "-");
		row_or_unknown(N_("keymap-xkb-model"), out, "X11 Model",
		               N_("xkb model"), "-");
	} else {
		rec_row("%s\t%s\t%s\t-",
		        N_("locale"), N_("unknown"), N_("localectl not installed"));
		rec_row("%s\t%s\t%s\t-",
		        N_("keymap-console"), N_("unknown"), N_("localectl not installed"));
		rec_row("%s\t%s\t%s\t-",
		        N_("keymap-xkb"), N_("unknown"), N_("localectl not installed"));
	}

	/* The clock used to be here — timezone, NTP, and nothing at all about how
	 * the time is WRITTEN. It moved to its own pane (src/time.c) when the
	 * desktop clock's 12/24-hour and date-layout settings arrived, rather than
	 * being duplicated: two panes offering the same editable row is how the
	 * two end up disagreeing about which one won. */

	return 0;
}
