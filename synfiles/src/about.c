/* about.c — what this program is, and which of its optional parts work here.
 *
 * Same shape as synpkg's About and for the same reason: nearly everything this
 * browser does beyond listing a directory depends on something that may not be
 * installed. Network places need gvfs, the Devices sidebar needs lsblk, file
 * types need shared-mime-info's database, opening a file needs xdg-open. When
 * one of them is absent the feature does not error — it is simply, silently
 * empty, and "there is nothing here" is a different claim from "this is
 * switched off on your machine".
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

static void about_row(const char *item, const char *state, const char *value,
                      const char *detail)
{
	if (g_out == OUT_REC) {
		rec_row(4, item, state, value, detail ? detail : "");
		return;
	}

	const char *colour = !strcmp(state, "ok")      ? C_ACCENT()
	                   : !strcmp(state, "off")     ? C_WARN()
	                   : !strcmp(state, "missing") ? C_DIM()
	                                               : C_ACCENT();

	printf("  %s%-14s%s %s%s%s\n", C_DIM(), item, C_RESET(), colour, value,
	       C_RESET());
	if (detail && *detail)
		printf("  %-14s %s%s%s\n", "", C_DIM(), detail, C_RESET());
}

int cmd_about(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	if (g_out == OUT_REC)
		rec_row(4, "item", "state", "value", "detail");
	else
		printf("\n  %ssynfiles — the SynapseOS file browser%s\n\n",
		       C_BOLD(), C_RESET());

	about_row("Version", "info", SYNFILES_VERSION, "GPL-2.0-or-later");
	about_row("Project", "info", "SynapseOS",
	          "https://github.com/velle999/SYNAPSE");

	/* The file type database. Without it every file falls back to a generic
	 * icon — degraded rather than broken, but it looks like a bug. */
	const char *globs = getenv("SYNFILES_GLOBS");
	if (!globs || !*globs)
		globs = "/usr/share/mime/globs2";
	if (access(globs, R_OK) == 0)
		about_row("File types", "ok", "shared-mime-info", globs);
	else
		about_row("File types", "missing", "no glob database",
		          "install shared-mime-info for real file icons");

	if (have_cmd("xdg-open"))
		about_row("Opening files", "ok", "xdg-open", "your desktop's own handlers");
	else
		about_row("Opening files", "missing", "xdg-open is not installed",
		          "install xdg-utils to open files");

	if (have_cmd("lsblk"))
		about_row("Devices", "ok", "lsblk", "disks and removable media");
	else
		about_row("Devices", "missing", "lsblk is not installed",
		          "install util-linux for the Devices sidebar");

	/* gvfs mounts appear as directories under /run/user/<uid>/gvfs, so the
	 * presence of that directory is the honest test — gvfs being installed
	 * but not running produces the same empty sidebar. */
	char *gvfs = xasprintf("/run/user/%lu/gvfs", (unsigned long)getuid());
	if (access(gvfs, F_OK) == 0)
		about_row("Network places", "ok", "gvfs is running", gvfs);
	else if (have_cmd("gio"))
		about_row("Network places", "off", "gvfs is installed but not running",
		          "mount a share and it will appear here");
	else
		about_row("Network places", "missing", "gvfs is not installed",
		          "install gvfs for SMB, SFTP and MTP shares");
	free(gvfs);

	char *data = xdg_data_home();
	char *places = xasprintf("%s/user-places.xbel", data);
	if (access(places, R_OK) == 0)
		about_row("Places", "ok", "shared with Dolphin", places);
	else
		about_row("Places", "off", "no bookmarks file yet",
		          "pinning a folder creates it");
	free(places);

	char *trash = xasprintf("%s/Trash", data);
	if (access(trash, F_OK) == 0)
		about_row("Trash", "ok", "XDG trash", trash);
	else
		about_row("Trash", "info", "nothing trashed yet", trash);
	free(trash);
	free(data);

	about_row("Front-ends", "info", "CLI and quickshell",
	          have_cmd("quickshell") ? "synfiles gui"
	                                 : "install quickshell for the GUI");

	/* A detail beginning https:// is a LINK. The GUI opens it in a browser
	 * rather than handing it to a shell, which is why "openable" and
	 * "runnable" are separate tests there and not one "clickable". */
	about_row("Support", "info", "Buy me a coffee", SYNAPSE_DONATE_URL);

	if (g_out == OUT_HUMAN)
		putchar('\n');
	return 0;
}
