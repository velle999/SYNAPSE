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
#include "i18n.h"
#include "config.h"

#include <string.h>

/* ⚠ `%-16s` PADS BY BYTES, AND THE VALUE COLUMN IS TRANSLATED NOW. "あり" is
 * six bytes and four terminal columns, so a printf width lines this table up
 * only while every value is ASCII — which they all were until the human view
 * started translating present/absent. term_cols() is what a terminal will
 * actually spend on it; the key column keeps its printf width because those are
 * protocol names and stay ASCII.  */
static void pad_col(const char *s, size_t cols)
{
	size_t w = term_cols(s);

	fputs(s, stdout);
	while (w++ < cols)
		putchar(' ');
}

static void row(const char *key, const char *val, const char *detail)
{
	if (g_out == OUT_REC) {
		rec_row(3, key, val, detail ? detail : "");
		return;
	}

	printf("  %s%-18s%s ", C_DIM(), key, C_RESET());
	pad_col(val, 16);
	printf(" %s%s%s\n", C_DIM(), detail ? detail : "", C_RESET());
}

/* ⛔ THE RECORD KEEPS THE ENGLISH, THE HUMAN VIEW TRANSLATES AT THE DRAW SITE.
 * `--rec` is what a script parses, and a third column that changes language
 * with the desktop is a record that cannot be compared between two machines —
 * the same rule synpkg's `--tsv` follows. So `what` arrives as an N_() msgid,
 * untranslated, and only the human branch runs it through gettext.
 * tests/i18n_test.sh proves it by RUNNING `--rec about` in German and in C and
 * diffing; it caught these three the moment they were marked with _().  */
static void tool_row(const char *name, const char *what)
{
	bool here = have_cmd(name);

	if (g_out == OUT_REC)
		row(name, here ? "present" : "absent", what);
	else
		row(name, here ? _("present") : _("absent"), _(what));
}

int cmd_about(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		die(_("about: unknown option '%s'"), argv[i]);

	if (g_out == OUT_REC)
		rec_row(3, "field", "value", "detail");
	else
		printf("%ssyn-edit %s%s — %s\n\n",
		       C_ACCENT(), SYNEDIT_VERSION, C_RESET(),
		       _("the SynapseOS text editor"));

	row("version", SYNEDIT_VERSION, "");
	row("licence", "GPL-2.0-or-later", "");
	row("project", "SynapseOS", "https://github.com/velle999/SYNAPSE");
	row("Support", "Buy me a coffee", SYNAPSE_DONATE_URL);

	char n[32];
	snprintf(n, sizeof n, "%zu", syn_lang_count());
	row("languages", n, g_out == OUT_REC ? "syn-edit langs lists them"
	                                     : _("syn-edit langs lists them"));

	if (g_out == OUT_HUMAN)
		printf("\n%s%s%s\n", C_BOLD(), _("What this machine can do"), C_RESET());

	tool_row("quickshell", N_("the graphical window (syn-edit gui)"));
	tool_row("wl-copy", N_("yanking to the desktop clipboard (\"+y)"));
	tool_row("wl-paste", N_("putting from the desktop clipboard (\"+p)"));

	/* Not a tool, but the same question: somebody wondering why the window
	 * and the terminal behave identically should find the answer here. */
	row("engine", "shared",
	    "the terminal and the window drive the same modal engine");
	row("regex", "POSIX",
	    "basic by default, extended after a leading \\v");

	return 0;
}
