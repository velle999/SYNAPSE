/*
 * main.c — flags and dispatch.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fputs(
"syn-arcade " SYNARCADE_VERSION " — the SynapseOS game assistant\n"
"\n"
"Usage: syn-arcade <command> [arguments]\n"
"\n"
"The overlay — frame rate, temperatures and load, over any game\n"
"  hud                     what the overlay is set to right now\n"
"  hud toggle              show it or hide it. Works INSIDE a running game\n"
"  hud show | hide         the two halves of toggle, for a script\n"
"  hud cycle               move it to the next corner (and unhide it)\n"
"  hud cycle-back          the other way round\n"
"  hud position [where]    read or set the corner by name\n"
"  hud set <key> <value>   any MangoHud setting — font_size, background_alpha\n"
"  hud path                which config file is in effect, and who outranks it\n"
"  hud adopt               take ownership of the overlay config\n"
"  hud ensure              create it if missing (your session runs this)\n"
"\n"
"Controllers\n"
"  pads                    every game controller attached\n"
"  pads info <pad>         buttons, axes, deadzones and live stick positions\n"
"  pads test <pad>         watch its buttons and sticks as you press them\n"
"       --seconds=N        how long to watch (default 30)\n"
"  pads rumble <pad>       check the motors\n"
"       --strong=N --weak=N --ms=N      percentages and duration\n"
"  pads calibrate <pad>    measure stick drift and set deadzones to cover it\n"
"       --seconds=N        how long to measure (default 5)\n"
"       --deadzone=N       skip measuring, set N%% on every stick\n"
"       --reset            clear the deadzones this set\n"
"  pads save <pad> --deadzone=N        remember it across replugs\n"
"  pads apply              re-apply saved deadzones (your session runs this)\n"
"  pads hold               hold every pad open until whoever started it goes,\n"
"                          so a wireless one does not fall asleep. Prints\n"
"                          nothing; the graphical window runs it for you.\n"
"\n"
"  A <pad> is its event id (event20), its number in the list (2), or any\n"
"  unique part of its name (dualsense).\n"
"\n"
"Controller mappings — for a pad whose buttons come out in the wrong places\n"
"  map                     the mappings you have added\n"
"  map learn [<pad>]       press each control once and write the mapping\n"
"  map add '<mapping>'     add one (quote it), replacing any for the same pad\n"
"  map remove <guid|name>  take one back out\n"
"  map path                where the database is, and whether SDL reads it\n"
"\n"
"Big screen mode — the ten-foot interface, for a television and a controller\n"
"  big                     whether it is running, and what it can launch\n"
"  big start               open it (Super+F10 once the shortcuts are in)\n"
"       --output=NAME      which screen: a connector name, `primary` or\n"
"                          `focused`. Default: primary, or whatever\n"
"                          `output =` says in ~/.config/syn-arcade/big.conf\n"
"  big stop                close it for good\n"
"  big show | hide         put it on screen, or get it out of the way while\n"
"                          leaving it running — this is what Guide does\n"
"  big toggle              the key's own behaviour: start it, or show/hide it\n"
"  big autostart on|off    open it at login, instead of the desktop\n"
"  big guide on|off        the pad's GUIDE button opens big screen mode from\n"
"                          the desktop (on by default; next login to apply)\n"
"  big steam               Steam Big Picture\n"
"       --gamescope[=WxH@R]  through gamescope, for a TV that is not your\n"
"                            desktop's resolution\n"
"  big games               every installed Steam game, most recent first\n"
"       --all              include Proton and the runtimes\n"
"  big launch <appid>      one of them, the way Steam's own shortcuts do\n"
"  big apps                the other tiles: launchers, media, browser,\n"
"                          terminal, power\n"
"  big run <id>            press one of them\n"
"       --wait             stay alive until it closes (the shell uses this to\n"
"                          know when to come back)\n"
"  big open <url>          a web page, in whichever browser is installed\n"
"  big windows             what is open — synui's list, not a tally kept here\n"
"  big focus <app-id>      switch to one of them\n"
"  big close <app-id>      close one of them\n"
"  big news                headlines for the news shelf\n"
"       --refresh          fetch now rather than reading the cache\n"
"  big media               Plex and Jellyfin servers on this network\n"
"       --refresh          ask again rather than reading the cache\n"
"  big music [status]      what is playing, for the Start menu's Now Playing\n"
"  big music play          start the player if it is not up, then play\n"
"  big music pause | toggle | next | prev | stop\n"
"  big music vis           the visualizer's bands, one NDJSON frame per line\n"
"                          (the Start menu's meter reads this)\n"
"                          ⚠ needs a player big screen mode can DRIVE rather\n"
"                          than launch — cliamp, which runs headless. Choose\n"
"                          it with `music = cliamp` in big.conf; any other\n"
"                          player is a tile that opens a window instead\n"
"  big nav                 controller input as words, one per line. The shell\n"
"                          reads this; nothing is synthesised into the desktop\n"
"  big guard               watch the pad's guide button all session. Your\n"
"                          session runs this; it does nothing while big screen\n"
"                          mode is already up\n"
"  big mouse               ⚠ the controller AS A MOUSE, for the browser. Real\n"
"                          pointer motion and clicks, so it runs only while\n"
"                          the shell is out of the way. See `big keys` too\n"
"  big keys                type what arrives on stdin (the on-screen keyboard)\n"
"  big listen              what has been sent to the running shell\n"
"\n"
"Shortcuts\n"
"  binds                   whether the gaming keys are installed, and which\n"
"  binds install           add them to synuirc\n"
"       --toggle=COMBO     default super+F11\n"
"       --cycle=COMBO      default super+F12\n"
"       --big=COMBO        default super+F10\n"
"       --reload           ask synui to re-read its config straight away\n"
"  binds refresh           add keys a newer syn-arcade defines to a block\n"
"                          that already exists, keeping the ones you chose\n"
"  binds guide on|off      the same setting as `big guide`\n"
"  binds remove            take them back out\n"
"  binds reload            re-read the compositor config now\n"
"\n"
"Front-ends\n"
"  gui                     the graphical window\n"
"  about                   version, licence, and what works on this machine\n"
"\n"
"Options\n"
"  --rec                   machine-readable records (what the GUI parses)\n"
"  -V, --version           print the version\n"
"  -h, --help              this text\n"
"\n"
"The overlay keys work inside a game that is already running. MangoHud lives\n"
"in the game's own process and watches its config file with inotify, so\n"
"rewriting that file reaches every running game at once — which is why these\n"
"are ordinary compositor shortcuts and not something the game has to support.\n"
"\n"
"⚠ MangoHud reads exactly ONE config file, and /etc/MangoHud.conf outranks\n"
"your own. `syn-arcade hud path` says which one is winning; `hud adopt` takes\n"
"ownership if the answer is not yours.\n"
"\n"
"Every field of --rec output is PERCENT-ENCODED, including the ones that look\n"
"like plain words: a controller name is arbitrary bytes off a USB descriptor.\n"
"Decode for display only — the encoded form is what to hand back.\n"
"\n"
"Exit status: 0 success, 1 failure, 2 usage, 100 \"nothing to list\".\n", f);
}

/*
 * The GUI is quickshell rendering data/syn-arcade.qml in a separate process,
 * for the same reason syn-disks, synfiles and synpkg do it that way: the binary
 * stays usable over SSH, and the window consumes exactly the records any other
 * consumer would.
 */
static int cmd_gui(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
		fputs("syn-arcade: no display — the gui needs a graphical "
		      "session\n", stderr);
		return EX_FAIL;
	}

	if (system("command -v quickshell >/dev/null 2>&1") != 0) {
		fputs("syn-arcade: quickshell is not installed — "
		      "synpkg install quickshell\n", stderr);
		return EX_FAIL;
	}

	/* The window's Wayland app_id. Without it quickshell names every window
	 * "org.quickshell", which is both the generic dock icon and the reason a
	 * dock cannot resolve a .desktop for the window.
	 *
	 * OVERWRITTEN, not merely set: an INHERITED QS_APP_ID is the common
	 * accident, because every app in this suite is a quickshell app that
	 * hands its whole environment to what it spawns. A window opened from
	 * another one would otherwise wear that app's identity. */
	setenv("QS_APP_ID", "syn-arcade", 1);

	const char *qml = SYNARCADE_DATADIR "/syn-arcade.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-arcade.qml", R_OK) == 0)
		qml = "data/syn-arcade.qml";	/* running from the source tree */

	execlp("quickshell", "quickshell", "-p", qml, (char *)NULL);

	fputs("syn-arcade: could not start quickshell\n", stderr);
	return EX_FAIL;
}

/* What works on THIS machine, which is the only version of this question worth
 * answering — every part of this tool depends on something that may not be
 * installed. */
static int cmd_about(bool rec)
{
	bool mangohud = system("command -v mangohud >/dev/null 2>&1") == 0;
	bool quickshell = system("command -v quickshell >/dev/null 2>&1") == 0;
	/* The on-screen keyboard types through wtype. Worth a line here because
	 * without it that half of big screen mode is silently inert — the
	 * keyboard draws, the keys highlight, and nothing is typed. */
	bool wtype = system("command -v wtype >/dev/null 2>&1") == 0;

	char hudpath[4096] = "";
	hud_effective_config(hudpath, sizeof(hudpath));
	bool hud_ok = mangohud && file_writable(hudpath);

	const char *sdl = getenv("SDL_GAMECONTROLLERCONFIG_FILE");

	if (rec) {
		rec_row(3, "field", "value", "action");
		rec_row(3, "version", SYNARCADE_VERSION, "detail");
		rec_row(3, "licence", "GPL-2.0-or-later", "detail");
		rec_row(3, "MangoHud", mangohud ? "installed" : "NOT INSTALLED",
			"detail");
		rec_row(3, "overlay config", hudpath, hud_ok ? "detail"
							     : "detail readonly");
		rec_row(3, "quickshell", quickshell ? "installed" : "NOT INSTALLED",
			"detail");
		rec_row(3, "on-screen keyboard", wtype ? "wtype installed"
						       : "wtype NOT INSTALLED",
			"detail");
		rec_row(3, "SDL mappings", (sdl && *sdl) ? sdl : "(not set)",
			"detail");
		return EX_OK;
	}

	printf("syn-arcade %s — the SynapseOS game assistant\n", SYNARCADE_VERSION);
	puts("GPL-2.0-or-later\n");
	printf("  MangoHud        %s\n", mangohud ? "installed"
	     : "NOT INSTALLED — the overlay does nothing without it");
	printf("  overlay config  %s%s\n", hudpath,
	       file_writable(hudpath) ? "" : "   (NOT WRITABLE — see `hud path`)");
	printf("  quickshell      %s\n", quickshell ? "installed"
	     : "NOT INSTALLED — `syn-arcade gui` needs it");
	printf("  wtype           %s\n", wtype ? "installed"
	     : "NOT INSTALLED — big screen mode's on-screen keyboard "
	       "cannot type without it");
	printf("  SDL mappings    %s\n", (sdl && *sdl) ? sdl
	     : "(SDL_GAMECONTROLLERCONFIG_FILE not set — see `map path`)");
	return EX_OK;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(stdout);
		return EX_OK;
	}

	/*
	 * ⚠ DROP AN INHERITED BIG-SCREEN LOCK BEFORE DOING ANYTHING ELSE.
	 *
	 * `big start` deliberately leaves its flock descriptor open across the
	 * exec into quickshell, so the kernel holds "big screen mode is running"
	 * for exactly as long as the shell lives. But quickshell passes the
	 * descriptor on to every helper it spawns, and any one of those
	 * outliving the shell keeps the lock — so the mode reports itself
	 * running with nothing on screen, permanently. See the note at the
	 * setenv in big.c for what that looked like in the wild.
	 *
	 * ⚠ THE VARIABLE IS UNSET AS WELL AS THE FD CLOSED, and that is not
	 * tidiness: this process may reuse the freed number for something of its
	 * own — pad.c opens event nodes seconds later — and a child of ours
	 * reading a stale SYN_BIG_LOCK_FD would then close a descriptor that
	 * means something entirely different.
	 */
	const char *lockfd = getenv("SYN_BIG_LOCK_FD");
	if (lockfd && *lockfd) {
		char *end = NULL;
		long n = strtol(lockfd, &end, 10);
		if (end && !*end && n > STDERR_FILENO && n < 1024)
			close((int)n);
		unsetenv("SYN_BIG_LOCK_FD");
	}

	const char *cmd = argv[1];
	int rest_c = argc - 2;
	char **rest = argv + 2;

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
		usage(stdout);
		return EX_OK;
	}
	if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version")) {
		puts(SYNARCADE_VERSION);
		return EX_OK;
	}

	if (!strcmp(cmd, "hud"))	return cmd_hud(rest_c, rest);
	if (!strcmp(cmd, "pads") || !strcmp(cmd, "pad"))
					return cmd_pads(rest_c, rest);
	if (!strcmp(cmd, "map"))	return cmd_map(rest_c, rest);
	if (!strcmp(cmd, "binds") || !strcmp(cmd, "bind"))
					return cmd_binds(rest_c, rest);
	if (!strcmp(cmd, "big"))	return cmd_big(rest_c, rest);
	if (!strcmp(cmd, "gui"))	return cmd_gui();
	if (!strcmp(cmd, "about"))
		return cmd_about(rest_c > 0 && !strcmp(rest[0], "--rec"));

	fprintf(stderr, "syn-arcade: unknown command '%s'\n", cmd);
	usage(stderr);
	return EX_USAGE;
}
