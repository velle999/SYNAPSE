/* main.c — flag parsing and dispatch.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
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
"  info <package>...       show everything known about a package\n"
"  install <package>...    install from the repositories\n"
"  remove <package>...     uninstall, with its unneeded dependencies\n"
"  upgrade                 refresh and upgrade the whole system\n"
"  refresh                 sync the package databases only\n"
"  updates                 list what a upgrade would change\n"
"  installed [--explicit]  what is on this machine\n"
"  orphans [--remove]      dependencies nothing needs any more\n"
"  status                  database and repository health\n"
"\n"
"Discovery\n"
"  suggest [category]      the curated SynapseOS software list\n"
"  suggest categories      just the category names\n"
"  groups [group]          browsable package groups, or one group's packages\n"
"\n"
"Other sources\n"
"  arsenal [subcommand]    BlackArch security tooling\n"
"  aur <search|install|installed|updates>\n"
"  flatpak <search|install|remove|installed|updates|remotes|enable-flathub>\n"
"  flatpak categories      browse Flathub by category\n"
"  flatpak category <name> the applications in one Flathub category\n"
"  system <check|apply>    SynapseOS's own components, via syn-update\n"
"\n"
"Front-ends\n"
"  tui                     browse in the terminal\n"
"  gui [tab]               open the graphical browser, optionally on one tab\n"
"                          (updates, suggested, repo, aur, flathub, arsenal,\n"
"                           system, about)\n"
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
	if (argc > 0 && *argv[0])
		setenv("SYNPKG_SECTION", argv[0], 1);

	const char *qml = SYNPKG_DATADIR "/synpkg.qml";
	if (access(qml, R_OK) != 0 && access("data/synpkg.qml", R_OK) == 0)
		qml = "data/synpkg.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
}

int main(int argc, char **argv)
{
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
	if (!strcmp(cmd, "install"))   return cmd_install(rest_argc, rest);
	if (!strcmp(cmd, "remove"))    return cmd_remove(rest_argc, rest);
	if (!strcmp(cmd, "upgrade"))   return cmd_upgrade(rest_argc, rest);
	if (!strcmp(cmd, "refresh"))   return cmd_refresh(rest_argc, rest);
	if (!strcmp(cmd, "updates"))   return cmd_updates(rest_argc, rest);
	if (!strcmp(cmd, "installed")) return cmd_installed(rest_argc, rest);
	if (!strcmp(cmd, "orphans"))   return cmd_orphans(rest_argc, rest);
	if (!strcmp(cmd, "status"))    return cmd_status(rest_argc, rest);
	if (!strcmp(cmd, "suggest"))   return cmd_suggest(rest_argc, rest);
	if (!strcmp(cmd, "groups"))    return cmd_groups(rest_argc, rest);
	if (!strcmp(cmd, "arsenal"))   return cmd_arsenal(rest_argc, rest);
	if (!strcmp(cmd, "aur"))       return cmd_aur(rest_argc, rest);
	if (!strcmp(cmd, "flatpak"))   return cmd_flatpak(rest_argc, rest);
	if (!strcmp(cmd, "system"))    return cmd_system(rest_argc, rest);
	if (!strcmp(cmd, "tui"))       return cmd_tui(rest_argc, rest);
	if (!strcmp(cmd, "gui"))       return cmd_gui(rest_argc, rest);
	if (!strcmp(cmd, "about"))     return cmd_about(rest_argc, rest);

	fprintf(stderr, "synpkg: unknown command '%s'\n\n", cmd);
	usage(stderr);
	return 2;
}
