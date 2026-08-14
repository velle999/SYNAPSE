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
#include <dirent.h>
#include <string.h>

static void controller(void)
{
	char out[8192] = "";
	char *argv[] = { (char *)"bluetoothctl", (char *)"show", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("controller\t-\tnone\t-\tno adapter, or bluetoothd is not running\t-");
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

	/* The adapter's power is the row worth acting on, and the VALUE column is
	 * where the editor reads current state from — so "Powered: yes" goes there
	 * rather than in `state`, with the adapter's name moved to detail. The
	 * first cut had the name in `value` and a toggle that read the name to
	 * decide which way to flip. */
	rec_row("controller\tpowered\t%s\t%s\t%s (%s)\ttoggle:bluetooth",
	        powered, addr, name, addr);
	rec_row("controller\tdiscoverable\t%s\t-\tvisible to anything scanning\t-", discoverable);
	rec_row("controller\tpairable\t%s\t-\taccepts new pairings\t-", pairable);
}

static void devices(void)
{
	char out[16384] = "";
	/* Paired, not scanned: a settings pane must not start a discovery sweep
	 * as a side effect of being opened. */
	char *argv[] = { (char *)"bluetoothctl", (char *)"devices",
	                 (char *)"Paired", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("device\t-\tnone paired\t-\tnothing has been paired with this adapter\t-");
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

		rec_row("device\t%s\t%s\t%s\tpaired\t-", addr, nbuf,
		        !strcmp(conn, "yes") ? "connected" : "not connected");
		any = 1;
	}
	if (!any)
		rec_row("device\t-\tnone paired\t-\tnothing has been paired with this adapter\t-");
}

/* Does this machine have a Bluetooth adapter?
 *
 * A directory read, so it cannot block and cannot be wrong about a machine
 * that has none: /sys/class/bluetooth exists only when the kernel has the
 * subsystem, and holds an hciN for each adapter. Asking bluetoothctl instead
 * is what hangs.
 *
 * SYN_SETTINGS_SYS_ROOT is for the test suite, which has to be able to pose a
 * machine without Bluetooth on a developer box that has it — the case this
 * exists for is the one this project's own hardware cannot show.
 */
static int bt_adapter_present(void)
{
	const char *root = getenv("SYN_SETTINGS_SYS_ROOT");
	char path[512];
	snprintf(path, sizeof path, "%s/sys/class/bluetooth",
	         (root && *root) ? root : "");

	DIR *d = opendir(path);
	if (!d)
		return 0;                       /* no subsystem: no adapter */

	int found = 0;
	struct dirent *e;
	while (!found && (e = readdir(d)))
		if (e->d_name[0] != '.') found = 1;
	closedir(d);
	return found;
}

static void radio(void)
{
	char out[4096] = "";
	char *argv[] = { (char *)"rfkill", (char *)"list", (char *)"bluetooth", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("radio\trfkill\tunknown\t-\trfkill reported nothing\t-");
		return;
	}

	char soft[64], hard[64];
	if (!scrape_field(out, "Soft blocked", soft, sizeof soft))
		snprintf(soft, sizeof soft, "unknown");
	if (!scrape_field(out, "Hard blocked", hard, sizeof hard))
		snprintf(hard, sizeof hard, "unknown");

	rec_row("radio\tsoft-block\t%s\t-\tsoftware; clearing it needs root (rfkill)\t-", soft);
	rec_row("radio\thard-block\t%s\t-\ta physical switch; software cannot clear it\t-", hard);
}

int pane_bluetooth(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (have_cmd("systemctl")) {
		char out[128] = "";
		char *argv[] = { (char *)"systemctl", (char *)"is-active",
		                 (char *)"bluetooth.service", NULL };
		run_capture(argv, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("service\tbluetooth.service\t%s\t-\tBlueZ; nothing below works without it\tunit:bluetooth.service",
		        out[0] ? out : "not installed");
	}

	if (have_cmd("rfkill")) radio();

	if (!have_cmd("bluetoothctl")) {
		rec_row("controller\t-\tunknown\t-\tbluez-utils is not installed\t-");
		return 0;
	}

	/* IS THERE AN ADAPTER AT ALL? Asked of the kernel, which cannot block.
	 *
	 * bluetoothctl talks to BlueZ over D-Bus, and on a machine with no adapter
	 * — every VM, and plenty of desktops — bluetooth.service is inactive and
	 * that call never returns. run_capture() now bounds it, so the pane can no
	 * longer be wedged by it; but ten seconds of nothing, twice, is still a
	 * pane that looks broken, and asking at all is pointless when the answer
	 * is knowable for free.
	 *
	 * /sys/class/bluetooth is the subsystem itself: absent when the kernel has
	 * no Bluetooth support, and empty when it has support and no hardware.
	 * Neither case has anything for bluetoothctl to describe. */
	if (!bt_adapter_present()) {
		rec_row("controller\t-\tno adapter\t-\tthis machine has no Bluetooth "
		        "hardware, so there is nothing to configure\t-");
		return 0;
	}

	controller();
	devices();

	/* The one quirk worth carrying into the UI rather than leaving in a wiki:
	 * on this hardware an AVRCP-capable sink announces full volume the moment
	 * it connects, which is loud and startling and is not a SynapseOS bug. */
	rec_row("note\tavrcp-volume\tsee detail\t-\t"
	        "a connecting headset may announce 100%% volume (AVRCP quirk); "
	        "SPA_DATA_DIR carries the wireplumber fix\t-");

	return 0;
}
