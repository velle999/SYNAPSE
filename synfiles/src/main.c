/* main.c — flags and dispatch.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fputs(
"synfiles " SYNFILES_VERSION " — the SynapseOS file browser\n"
"\n"
"Usage: synfiles [options] <command> [arguments]\n"
"\n"
"Browsing\n"
"  list [dir]              entries in a directory (default: .)\n"
"       -a, --all          include dotfiles\n"
"       -r, --reverse      reverse the sort\n"
"       --sort=KEY         name (default), size, mtime, type\n"
"       --no-dirs-first    do not float directories to the top\n"
"  info <path>             everything a properties pane shows\n"
"  find [dir] --name=GLOB [--content=TEXT] [--limit=N] [--max-depth=N]\n"
"                          search a tree; never follows symlinks\n"
"  actions <path>...       Open With entries and service menus that apply\n"
"  action <desktop> [id] -- <path>...\n"
"                          run one of them\n"
"\n"
"Sidebar\n"
"  places [list]           pinned folders\n"
"  places pin <path> [title]\n"
"  places unpin <path>\n"
"  recent [--limit=N] [--existing]\n"
"  volumes [--block|--network]\n"
"  mount <device>          mount a volume through udisks2\n"
"  unmount <device>        unmount it again\n"
"\n"
"Changing things\n"
"  trash <path>...         move to the trash (this is what Delete should do)\n"
"  trash list              what is in the trash\n"
"  trash restore <name>    put one back where it came from\n"
"  trash empty --yes       permanently remove everything in the trash\n"
"  copy [--conflict=P] <src>... <dir>\n"
"  move [--conflict=P] <src>... <dir>\n"
"  rename <path> <newname>\n"
"  mkdir <path>...\n"
"  delete --yes <path>...  PERMANENT, no trash, no undo\n"
"  compress [--format=F] [--name=N] <path>...\n"
"                          F is tar.gz (default), tar.xz, tar.zst, zip, 7z\n"
"  undo                    reverse the last thing that changed files\n"
"  undo list               what could be undone\n"
"\n"
"  P is error (default), skip, rename or overwrite. The default refuses and\n"
"  names the collision rather than guessing which file you meant to keep.\n"
"\n"
"Front-ends\n"
"  gui [dir]               the graphical browser\n"
"  about                   version, licence, and what works on this machine\n"
"\n"
"Options\n"
"  --rec                   machine-readable records (what the GUI parses)\n"
"  -v, --verbose           more detail on stderr\n"
"  --no-color              plain output even on a terminal\n"
"  -V, --version           print the version\n"
"  -h, --help              this text\n"
"\n"
"Paths in --rec output are PERCENT-ENCODED, because a filename may contain\n"
"tabs, newlines and bytes that are not valid UTF-8. Decode for display only:\n"
"the encoded form is the one to hand back to this program.\n"
"\n"
"Exit status: 0 success, 1 failure, 100 \"nothing to list\".\n", f);
}

/* The GUI is quickshell rendering data/synfiles.qml, in a separate process for
 * the same reason synpkg's is: the binary stays usable over SSH, and the GUI
 * consumes the same records any other consumer would. */
static int cmd_gui(int argc, char **argv)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("no display — synfiles gui needs a graphical session");
	if (!have_cmd("quickshell"))
		die("quickshell is not installed — synpkg install quickshell");

	/* The directory to open travels in the environment: quickshell takes no
	 * arguments of its own. */
	if (argc > 0 && *argv[0]) {
		char real[4096];
		if (realpath(argv[0], real))
			setenv("SYNFILES_DIR", real, 1);
		else
			setenv("SYNFILES_DIR", argv[0], 1);
	}

	const char *qml = SYNFILES_DATADIR "/synfiles.qml";
	if (access(qml, R_OK) != 0 && access("data/synfiles.qml", R_OK) == 0)
		qml = "data/synfiles.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
}

int main(int argc, char **argv)
{
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
			printf("synfiles " SYNFILES_VERSION "\n");
			return 0;
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else {
			break;
		}
	}

	if (i >= argc) {
		usage(stderr);
		return 2;
	}

	const char *cmd = argv[i++];
	int rest_argc = argc - i;
	char **rest = argv + i;

	if (!strcmp(cmd, "list"))    return cmd_list(rest_argc, rest);
	if (!strcmp(cmd, "info"))    return cmd_info(rest_argc, rest);
	if (!strcmp(cmd, "find"))    return cmd_find(rest_argc, rest);
	if (!strcmp(cmd, "places"))  return cmd_places(rest_argc, rest);
	if (!strcmp(cmd, "recent"))  return cmd_recent(rest_argc, rest);
	if (!strcmp(cmd, "volumes")) return cmd_volumes(rest_argc, rest);
	if (!strcmp(cmd, "trash"))   return cmd_trash(rest_argc, rest);
	if (!strcmp(cmd, "mount"))   return cmd_mount(rest_argc, rest);
	if (!strcmp(cmd, "unmount")) return cmd_unmount(rest_argc, rest);
	if (!strcmp(cmd, "copy"))    return cmd_copy(rest_argc, rest);
	if (!strcmp(cmd, "move"))    return cmd_move(rest_argc, rest);
	if (!strcmp(cmd, "rename"))  return cmd_rename(rest_argc, rest);
	if (!strcmp(cmd, "mkdir"))   return cmd_mkdir(rest_argc, rest);
	if (!strcmp(cmd, "delete"))  return cmd_delete(rest_argc, rest);
	if (!strcmp(cmd, "compress")) return cmd_compress(rest_argc, rest);
	if (!strcmp(cmd, "undo"))    return cmd_undo(rest_argc, rest);
	if (!strcmp(cmd, "actions")) return cmd_actions(rest_argc, rest);
	if (!strcmp(cmd, "action"))  return cmd_action(rest_argc, rest);
	if (!strcmp(cmd, "about"))   return cmd_about(rest_argc, rest);
	if (!strcmp(cmd, "gui"))     return cmd_gui(rest_argc, rest);

	fprintf(stderr, "synfiles: unknown command '%s'\n\n", cmd);
	usage(stderr);
	return 2;
}
