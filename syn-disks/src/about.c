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
#include "config.h"

#include <string.h>

static void row(const char *key, const char *val, const char *detail)
{
	if (g_out == OUT_REC)
		rec_row(3, key, val, detail ? detail : "");
	else
		printf("  %s%-18s%s %-14s %s%s%s\n", C_DIM(), key, C_RESET(), val,
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
		printf("%ssyn-disks %s%s — the SynapseOS disk utility\n\n",
		       C_ACCENT(), SYNDISKS_VERSION, C_RESET());

	row("version", SYNDISKS_VERSION, "");
	row("licence", "GPL-2.0-or-later", "");
	row("project", "SynapseOS", "https://github.com/velle999/SYNAPSE");
	row("Support", "Buy me a coffee", SYNAPSE_DONATE_URL);

	if (g_out == OUT_HUMAN)
		printf("\n%sWhat this machine can do%s\n", C_BOLD(), C_RESET());

	tool_row("lsblk", "filesystem types and labels");
	tool_row("udisksctl", "mount, unmount and safe removal");
	tool_row("smartctl", "drive health");
	tool_row("pkexec", "authorisation for formatting");
	tool_row("quickshell", "the graphical window");
	tool_row("mkfs.ext4", "format as ext4");
	tool_row("mkfs.btrfs", "format as btrfs");
	tool_row("mkfs.vfat", "format as FAT");
	tool_row("mkfs.exfat", "format as exFAT");
	tool_row("mkfs.ntfs", "format as NTFS");
	tool_row("mkfs.xfs", "format as XFS");

	/* Stated here as well as in actions.c, because this is the pane somebody
	 * reads when they are wondering why it refused. */
	row("system disk", "protected",
	    "formatting anything on the disk holding / is refused, with no override");

	return 0;
}
