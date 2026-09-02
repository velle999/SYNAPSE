/* main.c — flags and dispatch.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-edit.h"
#include "i18n.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fputs(
"syn-edit " SYNEDIT_VERSION " — the SynapseOS text editor\n"
"\n"
"Usage: syn-edit [options] [file...]\n"
"       syn-edit [options] <command> [arguments]\n"
"\n"
"With no command, syn-edit opens the files in the terminal editor. It is\n"
"modal: press i to insert, Escape to stop, :w to write and :q to quit.\n"
"\n"
"Editing\n"
"  tui [file...]           the terminal editor (the default)\n"
"  gui [file...]           the graphical window\n"
"\n"
"Scripting — the same engine, with no terminal\n"
"  run [file...]           apply keys to a file and print the result\n"
"      -k, --keys KEYS     key sequence, e.g. 'ggdG' or 'ciwfoo<Esc>'\n"
"      -c, --ex CMD        an ex command, e.g. '%s/a/b/g'; repeatable\n"
"      -w, --write         save the file instead of printing it\n"
"      --status            print the cursor, mode and message as records\n"
"      --lang NAME         force the language instead of guessing\n"
"  ex  [file...]           the same, reading only -c commands\n"
"\n"
"Looking\n"
"  highlight FILE          the syntax spans this file scans to\n"
"      --lang NAME         force the language\n"
"  langs                   every language that is highlighted\n"
"  config list|get|set|reset\n"
"                          persistent settings\n"
"  about                   version, licence, and what works on this machine\n"
"\n"
"Front-end plumbing\n"
"  serve [file...]         the engine on stdin/stdout; what the window drives\n"
"\n"
"Options\n"
"  --rec                   machine-readable records (what the GUI parses)\n"
"  -v, --verbose           more detail on stderr\n"
"  --no-color              plain output even on a terminal\n"
"  -V, --version           print the version\n"
"  -h, --help              this text\n"
"\n"
"KEYS use vim notation, with <Esc>, <CR>, <Tab>, <BS>, <C-r> and the arrow\n"
"keys spelled inside angle brackets. A literal < is <lt>.\n"
"\n"
"Patterns are POSIX BASIC regular expressions, which is the closest thing in\n"
"libc to vim's default: \\( \\) group, \\| alternates, \\+ and \\? repeat, and\n"
"\\< \\> anchor to word boundaries. A pattern starting with \\v is read as an\n"
"EXTENDED regular expression instead, which is what vim's very-magic mode is.\n"
"\n"
"Every field of --rec output is PERCENT-ENCODED, including the ones that look\n"
"like plain words: a line of source code can hold a tab, and a file name can\n"
"hold any byte at all. Decode for display only.\n"
"\n"
"Exit status: 0 success, 1 failure, 2 a usage problem.\n", f);
}

/* The GUI is quickshell rendering data/syn-edit.qml in a separate process, for
 * the same reason synfiles, synpkg and syn-disks do it that way: the binary
 * stays usable over SSH, and the window consumes exactly the records any other
 * consumer would. */
static int cmd_gui_impl(int argc, char **argv)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die(_("no display — syn-edit gui needs a graphical session "
		    "(plain `syn-edit` is the terminal editor)"));
	if (!have_cmd("quickshell"))
		die(_("quickshell is not installed — synpkg install quickshell"));

	/* The files to open travel in the environment; quickshell takes no
	 * arguments of its own. Separated by newlines rather than spaces or
	 * colons, because a file name may contain both. */
	if (argc > 0) {
		size_t len = 0;
		for (int i = 0; i < argc; i++)
			len += strlen(argv[i]) + 1;
		char *joined = xmalloc(len + 1);
		size_t w = 0;
		for (int i = 0; i < argc; i++) {
			size_t n = strlen(argv[i]);
			memcpy(joined + w, argv[i], n);
			w += n;
			joined[w++] = '\n';
		}
		joined[w] = '\0';
		setenv("SYN_EDIT_OPEN", joined, 1);
		free(joined);
	} else {
		/* No files means an EMPTY editor, and saying so takes an unsetenv:
		 * an inherited SYN_EDIT_OPEN would otherwise reopen whatever the
		 * process that spawned us was told to open. Same shape as the
		 * QS_APP_ID inheritance below — an environment-passed argument is
		 * only an argument to the process it was set for. */
		unsetenv("SYN_EDIT_OPEN");
	}

	/* The window's Wayland app_id. Without it quickshell names every one of
	 * its windows "org.quickshell", which is both the generic icon in the
	 * dock and the reason the dock cannot resolve a .desktop for the window —
	 * and on a miss the dock runs the app_id AS A COMMAND.
	 *
	 * OVERWRITTEN, not merely set. This is one process deciding what ITS OWN
	 * window is called, and no caller has ever had a reason to name it
	 * something else. It used to be setenv(..., 0) — "so a caller can still
	 * choose" — and the only thing that ever chose was an unrelated parent's
	 * leftover: synfiles is itself a quickshell app, so `xdg-open notes.txt`
	 * from its file list ran with QS_APP_ID=synfiles still in the
	 * environment and THIS window came up claiming to be synfiles.
	 *
	 * The dock keys entirely on app_id, so the editor was merged into the
	 * synfiles entry: no icon of its own appeared, and Close window on the
	 * dock closed whichever of the two it found first. Reported 2026-08-12 as
	 * "open a file with edit from files and it doesn't populate in the dock
	 * and i can't close it from the dock like normal". */
	setenv("QS_APP_ID", "syn-edit", 1);

	const char *qml = SYNEDIT_DATADIR "/syn-edit.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-edit.qml", R_OK) == 0)
		qml = "data/syn-edit.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die(_("could not start quickshell"));
}

int cmd_gui(int argc, char **argv) { return cmd_gui_impl(argc, argv); }

int main(int argc, char **argv)
{
	/* ⚠ BEFORE ANYTHING PRINTS. */
	syn_edit_i18n_init();

	g_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--rec")) {
			g_out = OUT_REC;
			g_color = false;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			g_verbose = true;
		} else if (!strcmp(a, "--no-color") || !strcmp(a, "--no-colour")) {
			g_color = false;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("syn-edit " SYNEDIT_VERSION "\n");
			return 0;
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "--")) {
			i++;
			break;
		} else {
			break;
		}
	}

	/* No arguments at all is the editor on an empty buffer, which is what
	 * typing the name of an editor means. */
	if (i >= argc)
		return cmd_tui(0, NULL);

	const char *cmd = argv[i];
	int rest_argc = argc - i - 1;
	char **rest = argv + i + 1;

	if (!strcmp(cmd, "tui"))       return cmd_tui(rest_argc, rest);
	if (!strcmp(cmd, "gui"))       return cmd_gui(rest_argc, rest);
	if (!strcmp(cmd, "run"))       return cmd_run(rest_argc, rest);
	if (!strcmp(cmd, "ex"))        return cmd_ex_cli(rest_argc, rest);
	if (!strcmp(cmd, "highlight")) return cmd_highlight(rest_argc, rest);
	if (!strcmp(cmd, "langs"))     return cmd_langs(rest_argc, rest);
	if (!strcmp(cmd, "config"))    return cmd_config(rest_argc, rest);
	if (!strcmp(cmd, "serve"))     return cmd_serve(rest_argc, rest);
	if (!strcmp(cmd, "about"))     return cmd_about(rest_argc, rest);

	/* Not a command, so it is a file name.
	 *
	 * ⚠ An unknown SUBCOMMAND and a mistyped FILE NAME are the same string,
	 * and this program cannot tell them apart — so it takes the reading that
	 * cannot lose work. Opening a buffer named "isntall" is recoverable in
	 * one keystroke; refusing to start because the name is not a command
	 * would make `syn-edit notes` fail for a file that exists. */
	if (cmd[0] == '-') {
		fprintf(stderr, _("syn-edit: unknown option '%s'\n\n"), cmd);
		usage(stderr);
		return 2;
	}
	return cmd_tui(argc - i, argv + i);
}
