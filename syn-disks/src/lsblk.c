/* lsblk.c — the handful of facts that are not in sysfs.
 *
 * Filesystem type, label, UUID and the GPT partition type name live in the
 * udev database rather than in /sys, because they are the result of probing
 * the device's content and the kernel does not do that. lsblk is the tool that
 * already reads that database, and its `-P` form — KEY="value" per line — is
 * explicitly designed to be parsed rather than read.
 *
 * ONE call, cached, for the whole tree. A lookup per device would fork lsblk
 * once per partition, which on a machine with four disks is eleven processes
 * to draw one window.
 *
 * MOUNTPOINT is asked for and deliberately IGNORED — see mounts.c for the
 * btrfs-subvolume reason. Everything here is enrichment: with lsblk absent the
 * storage tree is still complete, every device still has its size, kind, bus
 * and mounts, and only the type and label columns read "unknown".
 *
 * SYN_DISKS_LSBLK overrides the command name for the test suite, which puts a
 * fake one on PATH to describe hardware this machine does not have.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	char *name;
	lsblk_t v;
} entry_t;

static entry_t *g_ent;
static size_t   g_nent;
static bool     g_loaded;

static void load(void)
{
	if (g_loaded)
		return;
	g_loaded = true;

	const char *env = getenv("SYN_DISKS_LSBLK");
	const char *tool = (env && *env) ? env : "lsblk";
	if (!have_cmd(tool))
		return;

	char *argv[] = {
		(char *)tool, (char *)"-P", (char *)"-o",
		(char *)"NAME,FSTYPE,LABEL,UUID,PARTLABEL,PARTTYPENAME,PTTYPE,MODEL",
		NULL
	};
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return;
	}

	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);
	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		char *name = kv_val(lines[i], "NAME");
		if (!name)
			continue;
		g_ent = xrealloc(g_ent, (g_nent + 1) * sizeof *g_ent);
		g_ent[g_nent].name = name;
		g_ent[g_nent].v = (lsblk_t){
			.fstype    = kv_val(lines[i], "FSTYPE"),
			.label     = kv_val(lines[i], "LABEL"),
			.uuid      = kv_val(lines[i], "UUID"),
			.partlabel = kv_val(lines[i], "PARTLABEL"),
			/* PARTTYPENAME before PTTYPE is not an accident of order —
			 * kv_val anchors on the delimiters precisely so that asking
			 * for one cannot match inside the other. */
			.parttype  = kv_val(lines[i], "PARTTYPENAME"),
			.pttype    = kv_val(lines[i], "PTTYPE"),
			.model     = kv_val(lines[i], "MODEL"),
		};
		g_nent++;
	}
	free(lines);
	free(out);
}

static const lsblk_t g_empty;

const lsblk_t *lsblk_for(const char *kname)
{
	load();
	if (!kname)
		return &g_empty;

	for (size_t i = 0; i < g_nent; i++)
		if (!strcmp(g_ent[i].name, kname))
			return &g_ent[i].v;

	/* lsblk names a device-mapper volume by its dm name ("cryptroot") while
	 * the kernel calls it dm-0. Both spellings have to find the same row, or
	 * an unlocked LUKS volume shows no filesystem at all. */
	char *dm = sd_attr(kname, "dm/name");
	if (dm && *dm) {
		for (size_t i = 0; i < g_nent; i++)
			if (!strcmp(g_ent[i].name, dm)) {
				free(dm);
				return &g_ent[i].v;
			}
	}
	free(dm);

	return &g_empty;
}

void lsblk_done(void)
{
	for (size_t i = 0; i < g_nent; i++) {
		free(g_ent[i].name);
		free(g_ent[i].v.fstype);
		free(g_ent[i].v.label);
		free(g_ent[i].v.uuid);
		free(g_ent[i].v.partlabel);
		free(g_ent[i].v.parttype);
		free(g_ent[i].v.pttype);
		free(g_ent[i].v.model);
	}
	free(g_ent);
	g_ent = NULL;
	g_nent = 0;
	g_loaded = false;
}
