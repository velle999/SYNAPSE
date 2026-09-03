/* about.c — version, licence, and what actually works on this machine.
 *
 * The last part is the point. Every optional tool this program delegates to is
 * a capability that silently is not there — no smartmontools means no health,
 * no udisks2 means nothing mounts — and "why is that button missing" is a
 * question the program should be able to answer about itself.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "i18n.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>

/*
 * One `field <TAB> value <TAB> detail` row, or one aligned line for a person.
 *
 * ⛔ TRANSLATED AT THE DRAW SITE, NEVER IN THE ROW — the same rule the rest of
 * this program follows. `about --rec` is not read by the window, but it is a
 * record, and a script asking whether `smartctl` is `present` has to get the
 * same answer in every language.
 *
 * ⚠ A KEY IS SOMETIMES A COMMAND NAME. tool_row() passes `lsblk`, `mkfs.vfat`
 * and `pkexec`, which are never marked and so are never in the catalog; they
 * fall through this lookup unchanged, which is the right answer for them.
 */
static void row(const char *key, const char *val, const char *detail)
{
	if (g_out == OUT_REC)
		rec_row(3, key, val, detail ? detail : "");
	else
		printf("  %s%-18s%s %-14s %s%s%s\n", C_DIM(), _(key), C_RESET(),
		       _(val), C_DIM(), detail ? _(detail) : "", C_RESET());
}

static void tool_row(const char *name, const char *what)
{
	row(name, have_cmd(name) ? N_("present") : N_("absent"), what);
}

int cmd_about(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		die(_("about: unknown option '%s'"), argv[i]);

	if (g_out == OUT_REC)
		rec_row(3, "field", "value", "detail");
	else
		printf(_("%ssyn-disks %s%s — the SynapseOS disk utility\n\n"),
		       C_ACCENT(), SYNDISKS_VERSION, C_RESET());

	/* ⚠ A version, a licence identifier, a project name and a URL are DATA;
	 * only the words beside them are labels. */
	row(N_("version"), SYNDISKS_VERSION, "");
	row(N_("licence"), "GPL-2.0-or-later", "");
	row(N_("project"), "SynapseOS", "https://github.com/velle999/SYNAPSE");
	row(N_("Support"), N_("Buy me a coffee"), SYNAPSE_DONATE_URL);

	if (g_out == OUT_HUMAN)
		printf(_("\n%sWhat this machine can do%s\n"), C_BOLD(), C_RESET());

	tool_row("lsblk", N_("filesystem types and labels"));
	tool_row("udisksctl", N_("mount, unmount and safe removal"));
	tool_row("smartctl", N_("drive health"));
	tool_row("pkexec", N_("authorisation for formatting"));
	tool_row("quickshell", N_("the graphical window"));
	tool_row("mkfs.ext4", N_("format as ext4"));
	tool_row("mkfs.btrfs", N_("format as btrfs"));
	tool_row("mkfs.vfat", N_("format as FAT"));
	tool_row("mkfs.exfat", N_("format as exFAT"));
	tool_row("mkfs.ntfs", N_("format as NTFS"));
	tool_row("mkfs.xfs", N_("format as XFS"));

	/* Creating and mounting are two capabilities, and this pane used to
	 * report only the first. A machine with every mkfs installed and a kernel
	 * that had just been upgraded without a reboot read as fully capable,
	 * formatted a stick exFAT, and then could not mount it. */
	if (g_out == OUT_HUMAN)
		printf(_("\n%sWhat this kernel can mount%s\n"), C_BOLD(), C_RESET());

	size_t nfs = 0;
	const fs_kind_t *all = fs_all(&nfs);
	for (size_t i = 0; i < nfs; i++) {
		char *why = NULL;
		bool ok = fs_kernel_can_mount(&all[i], &why);
		char *key = xasprintf(_("mount %s"), all[i].name);
		/* ⚠ `key` is composed — "mount ext4" — and the TEMPLATE is the
		 * msgid, so the lookup happens before the substitution and works.
		 * `all[i].kmod` is a kernel module name and `why` is already a
		 * marked sentence from actions.c. */
		row(key, ok ? N_("yes") : N_("no"), ok ? all[i].kmod : why);
		free(key);
		free(why);
	}

	/* Stated here as well as in actions.c, because this is the pane somebody
	 * reads when they are wondering why it refused. */
	row(N_("system disk"), N_("protected"),
	    "formatting anything on the disk holding / is refused, with no override");

	return 0;
}
