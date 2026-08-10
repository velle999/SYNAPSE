/* syn-settings — the Bluetooth pane.
 *
 * `bluetoothctl show` and `devices` both run non-interactively and exit, which
 * is the only reason this is not a D-Bus client. If that ever stops being true
 * the right answer is BlueZ over D-Bus, not an expect script.
 *
 * rfkill is listed separately from "powered" because they fail differently and
 * look the same from the desk: a soft-blocked adapter and a powered-off one
 * are both "Bluetooth doesn't work", and only one of them is fixed by a
 * toggle in software. A hard block is a physical switch and no amount of
 * clicking will move it.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>

static void controller(void)
{
	char out[8192] = "";
	char *argv[] = { (char *)"bluetoothctl", (char *)"show", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("controller\t-\tnone\t-\tno adapter, or bluetoothd is not running");
		return;
	}

	/* "Controller AA:BB:CC:DD:EE:FF (public)" is the first line and the only
	 * one not in "Key: value" form, so it is read positionally. */
	char addr[64] = "unknown";
	if (!strncmp(out, "Controller ", 11))
		sscanf(out + 11, "%63s", addr);

	char name[128], powered[64], discoverable[64], pairable[64];
	if (!scrape_field(out, "Name", name, sizeof name)) snprintf(name, sizeof name, "unknown");
	if (!scrape_field(out, "Powered", powered, sizeof powered)) snprintf(powered, sizeof powered, "unknown");
	if (!scrape_field(out, "Discoverable", discoverable, sizeof discoverable)) snprintf(discoverable, sizeof discoverable, "unknown");
	if (!scrape_field(out, "Pairable", pairable, sizeof pairable)) snprintf(pairable, sizeof pairable, "unknown");

	rec_row("controller\t%s\t%s\t%s\tthe adapter", addr, name, powered);
	rec_row("controller\tdiscoverable\t%s\t-\tvisible to anything scanning", discoverable);
	rec_row("controller\tpairable\t%s\t-\taccepts new pairings", pairable);
}

static void devices(void)
{
	char out[16384] = "";
	/* Paired, not scanned: a settings pane must not start a discovery sweep
	 * as a side effect of being opened. */
	char *argv[] = { (char *)"bluetoothctl", (char *)"devices",
	                 (char *)"Paired", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("device\t-\tnone paired\t-\tnothing has been paired with this adapter");
		return;
	}

	int any = 0;
	for (char *line = out, *eol; line && *line; line = eol) {
		eol = strchr(line, '\n');
		if (eol) *eol++ = '\0';
		if (strncmp(line, "Device ", 7) != 0) continue;

		char addr[64] = "";
		if (sscanf(line + 7, "%63s", addr) != 1) continue;
		const char *name = line + 7 + strlen(addr);
		while (*name == ' ') name++;

		/* Connected is per device and is the state worth colouring: paired
		 * says it is known, connected says it is in use right now. */
		char info[8192] = "";
		char *ia[] = { (char *)"bluetoothctl", (char *)"info", addr, NULL };
		run_capture(ia, info, sizeof info);

		char conn[64];
		if (!scrape_field(info, "Connected", conn, sizeof conn))
			snprintf(conn, sizeof conn, "unknown");

		char nbuf[256];
		snprintf(nbuf, sizeof nbuf, "%s", *name ? name : "(unnamed)");
		tsv_clean(nbuf);

		rec_row("device\t%s\t%s\t%s\tpaired", addr, nbuf,
		        !strcmp(conn, "yes") ? "connected" : "not connected");
		any = 1;
	}
	if (!any)
		rec_row("device\t-\tnone paired\t-\tnothing has been paired with this adapter");
}

static void radio(void)
{
	char out[4096] = "";
	char *argv[] = { (char *)"rfkill", (char *)"list", (char *)"bluetooth", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("radio\trfkill\tunknown\t-\trfkill reported nothing");
		return;
	}

	char soft[64], hard[64];
	if (!scrape_field(out, "Soft blocked", soft, sizeof soft))
		snprintf(soft, sizeof soft, "unknown");
	if (!scrape_field(out, "Hard blocked", hard, sizeof hard))
		snprintf(hard, sizeof hard, "unknown");

	rec_row("radio\tsoft-block\t%s\t-\tsoftware; a toggle can clear this", soft);
	rec_row("radio\thard-block\t%s\t-\ta physical switch; software cannot clear it", hard);
}

int pane_bluetooth(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail");

	if (have_cmd("systemctl")) {
		char out[128] = "";
		char *argv[] = { (char *)"systemctl", (char *)"is-active",
		                 (char *)"bluetooth.service", NULL };
		run_capture(argv, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("service\tbluetooth.service\t%s\t-\tBlueZ; nothing below works without it",
		        out[0] ? out : "not installed");
	}

	if (have_cmd("rfkill")) radio();

	if (!have_cmd("bluetoothctl")) {
		rec_row("controller\t-\tunknown\t-\tbluez-utils is not installed");
		return 0;
	}
	controller();
	devices();

	/* The one quirk worth carrying into the UI rather than leaving in a wiki:
	 * on this hardware an AVRCP-capable sink announces full volume the moment
	 * it connects, which is loud and startling and is not a SynapseOS bug. */
	rec_row("note\tavrcp-volume\tsee detail\t-\t"
	        "a connecting headset may announce 100%% volume (AVRCP quirk); "
	        "SPA_DATA_DIR carries the wireplumber fix");

	return 0;
}
