/* table.c — the partition table, as geometry.
 *
 * ── Why this reads sysfs and not sfdisk ────────────────────────────────────
 *
 * `sfdisk --json /dev/nvme1n1` is the obvious way to get a partition table and
 * it is the wrong one here, because sfdisk opens the disk device — and that is
 * "Permission denied" for anybody who is not root. A window that had to raise
 * a polkit prompt before it could draw a bar chart of a drive would raise one
 * every time it opened, for the sake of showing somebody a picture. syn-disks
 * has one rule about privilege, and it is that LOOKING never asks.
 *
 * Every fact needed to draw the layout is already in sysfs and world-readable:
 * each partition directory holds `start` and `size`, both in 512-byte units,
 * and the disk's own `size` is the end of the map. The free space between
 * partitions is not stored anywhere by anyone — it is what is left when the
 * partitions are laid out in order, so it is derived here rather than read.
 *
 * sfdisk is used for WRITING and nothing else. See partition.c.
 *
 * ── The two units ──────────────────────────────────────────────────────────
 *
 * sysfs counts in 512-byte sectors and this file's structures are in BYTES,
 * converted once at the point of reading. A layout that mixed the two would
 * work perfectly on every drive until it met a 4Kn one, and then be wrong by a
 * factor of eight in whichever half had been left in sectors. sfdisk's script
 * format wants sectors again, and partition.c converts back at the one place
 * it writes them.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>

static int by_start(const void *a, const void *b)
{
	const pt_slot_t *x = a, *y = b;
	return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

unsigned long long pt_usable_end(const char *disk, const char *label)
{
	unsigned long long total = sd_size_bytes(disk);
	if (total == 0)
		return 0;

	/* GPT keeps a BACKUP copy of its header and of its whole entry array in
	 * the last 33 sectors of the disk. It is not a formality: it is the copy
	 * the kernel falls back to when the primary at the front is damaged, and
	 * a partition written over it destroys the recovery path at the same
	 * moment it destroys the thing that would have needed recovering.
	 *
	 * sfdisk knows this and would refuse — but it would refuse AFTER somebody
	 * had confirmed a destructive operation, which is the wrong place to find
	 * out that the size offered was never available. */
	if (label && !strcmp(label, "gpt")) {
		unsigned long long reserved = 33ULL * 512ULL;
		if (total <= reserved)
			return 0;
		return total - reserved - 1;
	}
	return total - 1;
}

pt_slot_t *pt_layout(const char *disk, size_t *n, const char **label)
{
	*n = 0;
	if (label)
		*label = "";

	const lsblk_t *lb = lsblk_for(disk);
	const char *ptt = (lb && lb->pttype) ? lb->pttype : "";
	if (label)
		*label = ptt;

	size_t np = 0;
	char **parts = sd_partitions(disk, &np);

	pt_slot_t *used = np ? xmalloc(np * sizeof *used) : NULL;
	size_t nu = 0;
	for (size_t i = 0; i < np; i++) {
		char *s = sd_attr(parts[i], "start");
		char *num = sd_attr(parts[i], "partition");
		unsigned long long bytes = sd_size_bytes(parts[i]);
		/* A partition with no `start` is not one this can place on a map, and
		 * guessing a position for it would put a box on screen that somebody
		 * could click. Skipped, so it appears as neither partition nor gap —
		 * the one honest answer available. */
		if (s && bytes > 0) {
			used[nu].kname  = xstrdup(parts[i]);
			used[nu].number = num ? (int)strtol(num, NULL, 10) : 0;
			used[nu].start  = strtoull(s, NULL, 10) * 512ULL;
			used[nu].bytes  = bytes;
			used[nu].gap    = false;
			nu++;
		}
		free(s);
		free(num);
	}
	sd_free_list(parts, np);

	/* sd_partitions orders by partition NUMBER, which is the order somebody
	 * refers to them by and not the order they sit in. Partition 3 routinely
	 * starts before partition 2 on a disk that has been repartitioned, and a
	 * map drawn in number order would show overlapping boxes and invent gaps
	 * that are not there. */
	if (nu > 1)
		qsort(used, nu, sizeof *used, by_start);

	unsigned long long end = pt_usable_end(disk, ptt);

	pt_slot_t *out = NULL;
	size_t no = 0;
	/* The first megabyte holds the MBR or the GPT header and its entry array,
	 * and is where every partitioning tool starts the first partition. Free
	 * space is counted from there, never from sector 0. */
	unsigned long long cursor = PT_ALIGN;

	for (size_t i = 0; i < nu; i++) {
		if (used[i].start > cursor && used[i].start - cursor >= PT_ALIGN) {
			out = xrealloc(out, (no + 1) * sizeof *out);
			out[no].kname  = xstrdup("");
			out[no].number = 0;
			out[no].start  = cursor;
			out[no].bytes  = used[i].start - cursor;
			out[no].gap    = true;
			no++;
		}
		out = xrealloc(out, (no + 1) * sizeof *out);
		out[no++] = used[i];           /* the kname string moves across */

		unsigned long long after = used[i].start + used[i].bytes;
		if (after > cursor)
			cursor = after;
	}
	free(used);

	if (end > cursor && end - cursor >= PT_ALIGN) {
		out = xrealloc(out, (no + 1) * sizeof *out);
		out[no].kname  = xstrdup("");
		out[no].number = 0;
		out[no].start  = cursor;
		out[no].bytes  = end - cursor + 1;
		out[no].gap    = true;
		no++;
	}

	*n = no;
	return out;
}

void pt_free_layout(pt_slot_t *v, size_t n)
{
	for (size_t i = 0; i < n; i++)
		free(v[i].kname);
	free(v);
}

/* ── the command ────────────────────────────────────────────────────────── */

/* The next free partition number on a table. GPT numbers are slots rather than
 * an ordering, so a disk with 1, 2 and 4 has 3 free and sfdisk is perfectly
 * happy to fill it. */
int pt_next_number(const char *disk)
{
	size_t np = 0;
	char **parts = sd_partitions(disk, &np);

	bool taken[129] = { false };
	for (size_t i = 0; i < np; i++) {
		char *num = sd_attr(parts[i], "partition");
		if (num) {
			long v = strtol(num, NULL, 10);
			if (v >= 1 && v <= 128)
				taken[v] = true;
			free(num);
		}
	}
	sd_free_list(parts, np);

	for (int i = 1; i <= 128; i++)
		if (!taken[i])
			return i;
	return 0;
}

int cmd_table(int argc, char **argv)
{
	if (argc < 1)
		die("table: need a disk (see: syn-disks list)");

	char *k = sd_kernel_name(argv[0]);
	if (!k)
		die("%s: not a block device", argv[0]);

	/* A partition names its disk perfectly well, and asking for the table of
	 * one is asking about the drive it is on. Refusing would be technically
	 * correct and useless. */
	if (sd_is_partition(k)) {
		char *parent = sd_parent_disk(k);
		if (parent) {
			free(k);
			k = parent;
		}
	}

	const char *ptt = "";
	size_t n = 0;
	pt_slot_t *slots = pt_layout(k, &n, &ptt);

	if (g_out == OUT_REC) {
		rec_row(9, "device", "number", "start", "bytes", "kind",
		        "fstype", "label", "mounts", "protected");
	} else {
		printf("%s%s%s  %s%s%s\n", C_BOLD(), k, C_RESET(),
		       C_DIM(), *ptt ? ptt : "no partition table", C_RESET());
	}

	for (size_t i = 0; i < n; i++) {
		char *sstart = xasprintf("%llu", slots[i].start);
		char *sbytes = xasprintf("%llu", slots[i].bytes);
		char *hu = human_size(slots[i].bytes);

		if (slots[i].gap) {
			if (g_out == OUT_REC)
				rec_row(9, "", "0", sstart, sbytes, "free", "", "", "", "");
			else
				printf("  %s%-16s%s %s%-16s%s %s%9s%s\n",
				       C_DIM(), "(free space)", C_RESET(),
				       C_DIM(), "", C_RESET(), C_DIM(), hu, C_RESET());
		} else {
			char *dev = xasprintf("/dev/%s", slots[i].kname);
			char *snum = xasprintf("%d", slots[i].number);
			const lsblk_t *lb = lsblk_for(slots[i].kname);

			size_t nm = 0;
			char **pts = mt_points_of(slots[i].kname, &nm);
			char *mounts = xstrdup(nm > 0 ? pts[0] : "");
			for (size_t j = 1; j < nm; j++) {
				char *g = xasprintf("%s, %s", mounts, pts[j]);
				free(mounts);
				mounts = g;
			}
			sd_free_list(pts, nm);

			/* The SAME call the refusal itself will make. `table` is what the
			 * GUI draws its buttons from, so a row saying nothing is in the
			 * way of deleting a partition, beside a binary that then refuses,
			 * is the exact failure this column exists to prevent. */
			char *why = guard_why_protected(slots[i].kname, GUARD_DESTROY);

			if (g_out == OUT_REC) {
				rec_row(9, dev, snum, sstart, sbytes, "partition",
				        lb && lb->fstype ? lb->fstype : "",
				        lb && lb->label ? lb->label : "",
				        mounts, why ? why : "");
			} else {
				printf("  %s%-16s%s %s%-16s%s %s%9s%s  %s%s%s\n",
				       C_ACCENT(), dev, C_RESET(),
				       lb && lb->fstype ? lb->fstype : "", "", "",
				       C_BOLD(), hu, C_RESET(),
				       why ? C_WARN() : C_DIM(),
				       why ? why : (*mounts ? mounts : ""), C_RESET());
			}

			free(why);
			free(mounts);
			free(snum);
			free(dev);
		}

		free(hu);
		free(sbytes);
		free(sstart);
	}

	pt_free_layout(slots, n);
	free(k);
	/* 100 is this program's "nothing to list", and an empty drive is a real
	 * answer rather than a failure. */
	return n > 0 ? 0 : 100;
}
