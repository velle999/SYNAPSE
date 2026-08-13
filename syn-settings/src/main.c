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
"  gui [pane]        open the settings window (display, region, time, network,\n"
"                    bluetooth, power, apps, kernel, system)\n"
"\n"
"  --rec display     connectors: kernel state beside what the compositor drives\n"
"  --rec region      keyboard layout and locale\n"
"  --rec time        the system clock — zone, NTP — and how the desktop\n"
"                    WRITES it: 12/24-hour, seconds, date order\n"
"  --rec power       sleep-critical units, sleep hooks, last suspend\n"
"  --rec system      identity, and WHERE configuration actually lives\n"
"  --rec network     interfaces, radios, and whether a firewall is up\n"
"  --rec bluetooth   adapter, radio blocks, and what is paired\n"
"  --rec kernel      every kernel on offer, which are installed, which runs\n"
"  --rec apps        the default application for each role, and WHICH file\n"
"                    decided it — a choice and a fallback read the same\n"
"                    everywhere else\n"
"\n"
"  set keymap <map>          console keymap        (localectl)\n"
"  set xkb <layout> [var]    desktop layout        (localectl)\n"
"  set locale <LANG>         system locale         (localectl)\n"
"  set timezone <zone>       time zone             (timedatectl)\n"
"  set ntp on|off            network time          (timedatectl)\n"
"  set time-format 12|24     how the desktop writes the time\n"
"  set time-seconds on|off   seconds in the bar clock\n"
"  set date-format <layout>  the date order — `syn-settings choices\n"
"                            date-format` lists them, with examples\n"
"  set app <role> <app>      default application for a role — the app is a\n"
"                            .desktop NAME, or a command for `terminal`\n"
"  unit <action> <name>      enable|disable|start|stop|restart (systemctl)\n"
"  device connect|disconnect <if>   bring an interface up or down (nmcli)\n"
"  probe <connector>         ask the kernel to re-detect a display (needs root)\n"
"  modes <connector>         list the modes that output can take\n"
"  apps <role>               list the applications that could take a role\n"
"  choices <key>             list what a setting can be set TO, with an\n"
"                            example of each: time-format, date-format\n"
"  mode <connector> <mode>   set one, e.g. DP-3 2560x1440@144 (wlr-randr)\n"
"  pkg install|remove <k>    add or remove a kernel, through synpkg\n"
"  boot <kernel> [--loader <name>] --confirm\n"
"                            make an installed kernel BOOTABLE: grub-mkconfig,\n"
"                            kernel-install, or limine-mkinitcpio-hook,\n"
"                            whichever this machine's bootloader needs\n"
"  default <kernel> [--loader <name>] --confirm\n"
"                            make an installed kernel the one that BOOTS:\n"
"                            limine's default_entry, bootctl set-default, or\n"
"                            grub-set-default\n"
"\n"
"  -n, --dry-run     print what would be run, change nothing\n"
"\n"
"Most writes are performed by a systemd tool that does its own polkit check;\n"
"this binary is not setuid and ships no polkit policy of its own. `boot` is\n"
"the exception in that it needs root outright, so it goes through pkexec —\n"
"which, with no policy shipped, means it asks for admin authentication. It\n"
"also refuses to do anything at all without --confirm.\n"
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
	if (!strcmp(cmd, "device")) return do_device(rest_argc, rest);
	if (!strcmp(cmd, "probe")) return do_probe(rest_argc, rest);
	if (!strcmp(cmd, "modes")) return do_modes(rest_argc, rest);
	if (!strcmp(cmd, "mode"))  return do_mode(rest_argc, rest);
	if (!strcmp(cmd, "pkg"))   return do_pkg(rest_argc, rest);
	if (!strcmp(cmd, "apps"))  return do_apps(rest_argc, rest);
	if (!strcmp(cmd, "choices")) return do_choices(rest_argc, rest);
	if (!strcmp(cmd, "boot"))  return do_boot(rest_argc, rest);
	if (!strcmp(cmd, "default")) return do_default(rest_argc, rest);

	if (!strcmp(cmd, "--rec")) {
		if (rest_argc < 1) { usage(); return 2; }
		const char *pane = rest[0];
		if (!strcmp(pane, "display")) return pane_display();
		if (!strcmp(pane, "region"))  return pane_region();
		if (!strcmp(pane, "time"))    return pane_time();
		if (!strcmp(pane, "power"))   return pane_power();
		if (!strcmp(pane, "system"))  return pane_system();
		if (!strcmp(pane, "network"))   return pane_network();
		if (!strcmp(pane, "bluetooth")) return pane_bluetooth();
		if (!strcmp(pane, "kernel"))    return pane_kernel();
		if (!strcmp(pane, "apps"))      return pane_apps();
		fprintf(stderr, "syn-settings: unknown pane '%s'\n", pane);
		return 2;
	}

	fprintf(stderr, "syn-settings: unknown command '%s'\n", cmd);
	usage();
	return 2;
}
