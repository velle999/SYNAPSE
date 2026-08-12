/* about.c — version, licence, and what works on this machine.
 *
 * The last part is the point, as it is in syn-disks: every optional tool this
 * program delegates to is a capability that silently is not there, and "why is
 * that missing" is a question the program should be able to answer about
 * itself rather than leaving to a support thread.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-edit.h"
#include "config.h"

#include <string.h>

static void row(const char *key, const char *val, const char *detail)
{
	if (g_out == OUT_REC)
		rec_row(3, key, val, detail ? detail : "");
	else
		printf("  %s%-18s%s %-16s %s%s%s\n", C_DIM(), key, C_RESET(), val,
		       C_DIM(), detail ? detail : "", C_RESET());
}

static void tool_row(const char *name, const char *what)
{
	row(name, have_cmd(name) ? "present" : "absent", what);
}

int cmd_about(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		die("about: unknown option '%s'", argv[i]);

	if (g_out == OUT_REC)
		rec_row(3, "field", "value", "detail");
	else
		printf("%ssyn-edit %s%s — the SynapseOS text editor\n\n",
		       C_ACCENT(), SYNEDIT_VERSION, C_RESET());

	row("version", SYNEDIT_VERSION, "");
	row("licence", "GPL-2.0-or-later", "");
	row("project", "SynapseOS", "https://github.com/velle999/SYNAPSE");
	row("Support", "Buy me a coffee", SYNAPSE_DONATE_URL);

	char n[32];
	snprintf(n, sizeof n, "%zu", syn_lang_count());
	row("languages", n, "syn-edit langs lists them");

	if (g_out == OUT_HUMAN)
		printf("\n%sWhat this machine can do%s\n", C_BOLD(), C_RESET());

	tool_row("quickshell", "the graphical window (syn-edit gui)");
	tool_row("wl-copy", "yanking to the desktop clipboard (\"+y)");
	tool_row("wl-paste", "putting from the desktop clipboard (\"+p)");

	/* Not a tool, but the same question: somebody wondering why the window
	 * and the terminal behave identically should find the answer here. */
	row("engine", "shared",
	    "the terminal and the window drive the same modal engine");
	row("regex", "POSIX",
	    "basic by default, extended after a leading \\v");

	return 0;
}
