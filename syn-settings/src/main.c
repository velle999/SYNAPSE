/* syn-settings — SynapseOS system settings.
 *
 * The configuration surface of this OS is spread across the control panel,
 * synuirc, the installer, systemd units, sysctl drop-ins and raw CLI, and
 * several of its worst bugs have been configuration bugs that nothing SHOWED:
 * sleep units present but disabled after a reinstall, the installer setting a
 * console keymap while the desktop read the xkb one, a shipped sysctl file
 * quietly overridden from /etc.
 *
 * So the first job of this app is not to change settings. It is to display
 * the true current state, from the authority that owns it, with the layer it
 * came from. Writing is the smaller half and is handed to tools that already
 * do their own authorisation.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
	printf(
"syn-settings " SYNSETTINGS_VERSION " — SynapseOS system settings\n"
"\n"
"usage: syn-settings <command> [args]\n"
"\n"
"  gui [pane]        open the settings window (display, region, power, system)\n"
"\n"
"  --rec display     connectors: kernel state beside what the compositor drives\n"
"  --rec region      keyboard, locale, timezone, NTP\n"
"  --rec power       sleep-critical units, sleep hooks, last suspend\n"
"  --rec system      identity, and WHERE configuration actually lives\n"
"\n"
"  set keymap <map>          console keymap        (localectl)\n"
"  set xkb <layout> [var]    desktop layout        (localectl)\n"
"  set locale <LANG>         system locale         (localectl)\n"
"  set timezone <zone>       time zone             (timedatectl)\n"
"  set ntp on|off            network time          (timedatectl)\n"
"  unit <action> <name>      enable|disable|start|stop|restart (systemctl)\n"
"\n"
"  -n, --dry-run     print what would be run, change nothing\n"
"\n"
"Every write is performed by a systemd tool that does its own polkit check.\n"
"This binary is not setuid and grants no privilege of its own: if localectl\n"
"would refuse you at a terminal, it refuses you here.\n"
"\n"
"--rec prints TSV — a header line, then rows. `syn-settings --rec region |\n"
"column -t -s$'\\t'` is meant to be a useful thing to type.\n"
"\n"
"Exit status: 0 success, 1 failure, 2 refused (bad argument).\n");
}

/* The GUI is a separate process rendering the same TSV any other consumer
 * would parse, exactly as synpkg and synfiles do it. The C binary stays usable
 * on a headless box and over SSH, which is where a settings tool is often
 * needed most. */
static int cmd_gui(int argc, char **argv)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
		fprintf(stderr, "syn-settings: no display — try `syn-settings --rec system`\n");
		return 1;
	}
	if (!have_cmd("quickshell")) {
		fprintf(stderr, "syn-settings: quickshell is not installed\n");
		return 1;
	}

	/* Which pane to open on travels in the environment: quickshell takes no
	 * arguments of its own, and the start menu wants to point separate
	 * entries at separate panes. An unknown name is ignored by the QML rather
	 * than refused here — failing to open the window over a typo is a poor
	 * trade for a convenience. */
	if (argc > 0 && *argv[0])
		setenv("SYNSETTINGS_PANE", argv[0], 1);

	/* The window's Wayland app_id. Without it quickshell names every window
	 * it opens "org.quickshell", so the dock cannot tell this app from any
	 * other QML app on the system, draws quickshell's generic icon, and
	 * offers no "New Window". Set, not overridden, so a caller can choose. */
	setenv("QS_APP_ID", "syn-settings", 0);

	const char *qml = SYNSETTINGS_DATADIR "/syn-settings.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-settings.qml", R_OK) == 0)
		qml = "data/syn-settings.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	fprintf(stderr, "syn-settings: could not start quickshell\n");
	return 1;
}

int main(int argc, char **argv)
{
	int i = 1;
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--dry-run"))
			g_dry_run = 1;
		else
			break;
	}

	if (i >= argc) { usage(); return 0; }

	const char *cmd = argv[i];
	int rest_argc = argc - i - 1;
	char **rest = argv + i + 1;

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
		usage();
		return 0;
	}
	if (!strcmp(cmd, "--version")) {
		printf("syn-settings " SYNSETTINGS_VERSION "\n");
		return 0;
	}
	if (!strcmp(cmd, "gui"))  return cmd_gui(rest_argc, rest);
	if (!strcmp(cmd, "set"))  return do_set(rest_argc, rest);
	if (!strcmp(cmd, "unit")) return do_unit(rest_argc, rest);

	if (!strcmp(cmd, "--rec")) {
		if (rest_argc < 1) { usage(); return 2; }
		const char *pane = rest[0];
		if (!strcmp(pane, "display")) return pane_display();
		if (!strcmp(pane, "region"))  return pane_region();
		if (!strcmp(pane, "power"))   return pane_power();
		if (!strcmp(pane, "system"))  return pane_system();
		fprintf(stderr, "syn-settings: unknown pane '%s'\n", pane);
		return 2;
	}

	fprintf(stderr, "syn-settings: unknown command '%s'\n", cmd);
	usage();
	return 2;
}
