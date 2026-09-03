/* main.c — flag parsing and dispatch.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *out)
{
	fprintf(out,
"synpkg " SYNPKG_VERSION " — the SynapseOS package manager\n"
"\n"
"Usage: synpkg [options] <command> [arguments]\n"
"\n"
"Packages\n"
"  search <term>...        search the repositories\n"
"    --all                 every source at once: the repositories, BlackArch,\n"
"                          the AUR and Flathub, in one list, each result\n"
"                          labelled with where it came from\n"
"    --aur                 append AUR results to the repository ones\n"
"    --installed           search only what is already installed\n"
"  info <package>...       show everything known about a package\n"
"  provides <term>         what to install to get a program called <term>,\n"
"                          best match first. What the start menu and the\n"
"                          command bar ask when a name matches nothing that\n"
"                          is installed. Repositories only, so it answers at\n"
"                          typing speed; --limit N caps the list\n"
"  install <package>...    install from the repositories, falling back to the\n"
"                          AUR for a name no repository carries (--no-aur\n"
"                          to refuse rather than build from source)\n"
"  remove <package>...     uninstall, with its unneeded dependencies\n"
"    --owner               the arguments are APPLICATIONS, not packages: a\n"
"                          .desktop path or a freedesktop entry id, resolved\n"
"                          to whatever owns it. What the Uninstall row in the\n"
"                          start menu and the application grid runs\n"
"  upgrade                 force-refresh the databases (-Syy, under the one\n"
"                          authentication the upgrade already needs) and\n"
"                          upgrade the whole system, then rebuild\n"
"                          the AUR packages synpkg installed (--no-aur skips),\n"
"                          then the SynapseOS components (--no-system skips).\n"
"                          Shows Arch news published since your last upgrade\n"
"                          first, and asks (--no-news skips), and offers a\n"
"                          reboot if the running kernel was replaced\n"
"  refresh                 sync the package databases only\n"
"  news [--all]            Arch news since your last upgrade, or the latest 10\n"
"  updates                 list what a upgrade would change, with held-back\n"
"                          ones marked rather than hidden\n"
"  ignore [package]...     hold a package back. With names, stop upgrading\n"
"                          them; with none, list everything held back and\n"
"                          whether an update is waiting for it. The list is\n"
"                          pacman.conf's IgnorePkg, so plain `pacman -Syu`\n"
"                          honours it too\n"
"  unignore <package>...   let one go again\n"
"  installed [--explicit]  what is on this machine\n"
"  orphans [--remove]      dependencies nothing needs any more\n"
"  owner <file|app-id>...  which package an application came from. Takes a\n"
"                          .desktop path or a freedesktop entry id, prints\n"
"                          the package name alone on stdout, and exits 1 for\n"
"                          anything no package owns. A Flatpak answers with\n"
"                          its application id\n"
"  status                  database and repository health\n"
"\n"
"Discovery\n"
"  suggest [category]      the curated SynapseOS software list\n"
"  suggest categories      just the category names\n"
"  groups [group]          browsable package groups, or one group's packages\n"
"\n"
"Other sources\n"
"  arsenal [subcommand]    BlackArch security tooling\n"
"  cachyos [subcommand]    the CachyOS kernel repository\n"
"                          (status, enable-repo, disable-repo)\n"
"  aur <search|install|installed|updates>\n"
"  flatpak <search|install|remove|installed|updates|remotes|enable-flathub>\n"
"  flatpak ignore|unignore [app]   hold a Flatpak back (flatpak mask)\n"
"  flatpak categories      browse Flathub by category\n"
"  flatpak category <name> the applications in one Flathub category\n"
"  appimage install <file> | list | remove <name>\n"
"                          an AppImage: place it in ~/Applications, put its\n"
"                          menu entry and icons where the desktop looks, and\n"
"                          record what was placed so remove is a real\n"
"                          uninstall.\n"
"                          ⛔ NOT a fourth source. AppImages have no index, so\n"
"                          `search` cannot reach them; they carry no update\n"
"                          information, so `updates` never mentions them; and\n"
"                          they are unsigned. Install a newer file over the\n"
"                          top to upgrade one\n"
"  system <check|apply|ignore|unignore|ignored>\n"
"                          SynapseOS's own components, via syn-update\n"
"                           apply takes component names: system apply synui\n"
"  config [key [yes|no]]   what upgrade does by default; no arguments lists\n"
"\n"
"Front-ends\n"
"  tui                     browse in the terminal\n"
"  gui [tab] [--search T]  open the graphical browser, optionally on one tab\n"
"                          (updates, suggested, repo, aur, flathub, arsenal,\n"
"                           system, about) and already searching for T\n"
"  about                   version, licence, and which sources are enabled\n"
"\n"
"Options\n"
"  --tsv                   machine-readable output (what the GUI parses)\n"
"  -y, --noconfirm         never prompt\n"
"  -v, --verbose           more detail on stderr\n"
"  --no-color              plain output even on a terminal\n"
"  -V, --version           print the version\n"
"  -h, --help              this text\n"
"\n"
"Exit status: 0 success, 1 failure, 100 \"nothing to do\" for the list\n"
"commands, so a poller can branch without parsing.\n");
}

/* The graphical front-end is quickshell rendering data/synpkg.qml. It is a
 * separate process on purpose: the C binary stays usable on a headless box and
 * over SSH, and the GUI is the same TSV any other consumer would parse. */
static int cmd_gui(int argc, char **argv)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
		warn("no display — falling back to the terminal browser");
		return cmd_tui(0, NULL);
	}
	if (!have_cmd("quickshell")) {
		warn("quickshell is not installed — falling back to the terminal browser");
		return cmd_tui(0, NULL);
	}

	/* `synpkg gui flathub` opens on that tab. The start menu wants to point
	 * separate entries at separate sources, and quickshell takes no arguments
	 * of its own, so the tab travels in the environment. An unknown name is
	 * ignored by the QML rather than refused here: this is a convenience, and
	 * failing to open the window over a typo is a poor trade. */
	/*
	 * `synpkg gui [tab] [--search <term>]`.
	 *
	 * --search is what synui's start menu and command bar hand over when they
	 * have run out of local answers: `provides` asks the repositories only,
	 * because it runs while somebody types, and the row that ends its list
	 * means "now ask everywhere" — the AUR and Flathub included. Handing the
	 * term over rather than making the user type it again is the whole point
	 * of that row.
	 *
	 * Both travel in the ENVIRONMENT because this function execs quickshell,
	 * which takes no arguments of its own. See data/synpkg.qml, which reads
	 * SYNPKG_SECTION and SYNPKG_QUERY at startup.
	 */
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--search")) {
			if (++i >= argc)
				die("gui: --search needs a term");
			setenv("SYNPKG_QUERY", argv[i], 1);
		} else if (*argv[i]) {
			setenv("SYNPKG_SECTION", argv[i], 1);
		}
	}

	/* The window's Wayland app_id, which is how the dock, the taskbar and
	 * every other window-to-application mapping finds out WHAT this window
	 * is. Without it quickshell names every one of its windows
	 * "org.quickshell" — so the dock drew quickshell's own generic icon for
	 * this app, could not resolve a .desktop for it, and therefore offered no
	 * "New Window" either: synpkg and every other QML app on the system were one
	 * indistinguishable entry.
	 *
	 * OVERWRITTEN, not merely set. This is one process deciding what ITS OWN
	 * window is called, and no caller has ever had a reason to name it
	 * something else. An INHERITED value is the real and common accident:
	 * every one of these apps is a quickshell app that hands its whole
	 * environment to what it spawns, so launching this one from another gave
	 * it the OTHER app's identity and no dock entry of its own. */
	setenv("QS_APP_ID", "synpkg", 1);

	const char *qml = SYNPKG_DATADIR "/synpkg.qml";
	if (access(qml, R_OK) != 0 && access("data/synpkg.qml", R_OK) == 0)
		qml = "data/synpkg.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
}

int main(int argc, char **argv)
{
	/* ⚠ BEFORE ANYTHING PRINTS, including the colour decision's own errors. */
	synpkg_i18n_init();

	/* Colour is decided before flag parsing so an error inside it is still
	 * readable, and honours NO_COLOR (https://no-color.org). */
	g_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--tsv")) {
			g_out = OUT_TSV;
			g_color = false;
		} else if (!strcmp(a, "-y") || !strcmp(a, "--noconfirm")) {
			g_noconfirm = true;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			g_verbose = true;
		} else if (!strcmp(a, "--no-color") || !strcmp(a, "--no-colour")) {
			g_color = false;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("synpkg " SYNPKG_VERSION "\n");
			return 0;
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else {
			break;   /* first non-option is the command */
		}
	}

	if (i >= argc) {
		/* Bare `synpkg` on a terminal is a browse request; piped, it is a
		 * mistake worth a usage message rather than an interactive prompt
		 * nobody can answer. */
		if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
			return cmd_tui(0, NULL);
		usage(stderr);
		return 2;
	}

	const char *cmd = argv[i++];
	int rest_argc = argc - i;
	char **rest = argv + i;

	if (!strcmp(cmd, "search"))    return cmd_search(rest_argc, rest);
	if (!strcmp(cmd, "info"))      return cmd_info(rest_argc, rest);
	if (!strcmp(cmd, "provides"))  return cmd_provides(rest_argc, rest);
	if (!strcmp(cmd, "install"))   return cmd_install(rest_argc, rest);
	if (!strcmp(cmd, "remove"))    return cmd_remove(rest_argc, rest);
	if (!strcmp(cmd, "upgrade"))   return cmd_upgrade(rest_argc, rest);
	if (!strcmp(cmd, "refresh"))   return cmd_refresh(rest_argc, rest);
	if (!strcmp(cmd, "news"))      return cmd_news(rest_argc, rest);
	if (!strcmp(cmd, "updates"))   return cmd_updates(rest_argc, rest);
	if (!strcmp(cmd, "installed")) return cmd_installed(rest_argc, rest);
	if (!strcmp(cmd, "orphans"))   return cmd_orphans(rest_argc, rest);
	if (!strcmp(cmd, "owner"))     return cmd_owner(rest_argc, rest);
	if (!strcmp(cmd, "status"))    return cmd_status(rest_argc, rest);
	if (!strcmp(cmd, "suggest"))   return cmd_suggest(rest_argc, rest);
	if (!strcmp(cmd, "groups"))    return cmd_groups(rest_argc, rest);
	if (!strcmp(cmd, "arsenal"))   return cmd_arsenal(rest_argc, rest);
	if (!strcmp(cmd, "cachyos"))   return cmd_cachyos(rest_argc, rest);
	if (!strcmp(cmd, "appimage"))  return cmd_appimage(rest_argc, rest);
	if (!strcmp(cmd, "aur"))       return cmd_aur(rest_argc, rest);
	if (!strcmp(cmd, "flatpak"))   return cmd_flatpak(rest_argc, rest);
	if (!strcmp(cmd, "system"))    return cmd_system(rest_argc, rest);
	if (!strcmp(cmd, "ignore"))    return cmd_ignore(rest_argc, rest);
	if (!strcmp(cmd, "unignore"))  return cmd_unignore(rest_argc, rest);
	if (!strcmp(cmd, "config"))    return cmd_config(rest_argc, rest);
	if (!strcmp(cmd, "tui"))       return cmd_tui(rest_argc, rest);
	if (!strcmp(cmd, "gui"))       return cmd_gui(rest_argc, rest);
	if (!strcmp(cmd, "about"))     return cmd_about(rest_argc, rest);

	fprintf(stderr, "synpkg: unknown command '%s'\n\n", cmd);
	usage(stderr);
	return 2;
}
