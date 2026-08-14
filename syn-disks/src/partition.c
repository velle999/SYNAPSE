/* partition.c — the four operations that change a partition table.
 *
 * Every one of these is destructive to some degree, so all four are built the
 * same way and the shape is worth stating once:
 *
 *   1. Resolve the argument to a kernel name. Nothing downstream ever sees the
 *      string the user typed.
 *   2. Ask guard.c. It is the ONLY place the rules live; nothing here decides
 *      for itself what may be touched. See the header of guard.c for what the
 *      rules are and why partitioning's are narrower than format's.
 *   3. Build the sfdisk invocation and its script ONCE.
 *   4. Print it, or run it. --dry-run prints, --yes runs, and neither can show
 *      something different from what the other would do because there is one
 *      command and one script between them.
 *
 * ── Why sfdisk, and why it never sees a shell ──────────────────────────────
 *
 * sfdisk is the tool that owns writing partition tables, the same way udisks2
 * owns mounting and smartctl owns health. Reimplementing GPT here would mean
 * writing a CRC32 over a structure that, if it is wrong, makes a drive look
 * blank.
 *
 * Its script arrives on STDIN through run_capture_in, never as a command line
 * and never through a shell. That matters most for the one field that is not a
 * number: a partition name. "start=2048, size=1024, name=..." is data on a
 * pipe, so whatever is in that name is a partition name and cannot be anything
 * else.
 *
 * ── Why shrinking is refused ───────────────────────────────────────────────
 *
 * resize will GROW a partition and will not shrink one. A partition is a
 * window onto a disk and the filesystem inside it has its own idea of where it
 * ends; moving the window in leaves the filesystem believing in blocks that
 * are no longer inside it, which is not a warning at the time and is a
 * destroyed filesystem the next time it writes near the end. Doing it properly
 * means shrinking the filesystem FIRST, with a tool that differs per
 * filesystem and cannot do it online for most of them. That is a real feature
 * and it is not this one; offering it as a flag here would be offering data
 * loss with a confirmation dialogue in front of it.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── shared plumbing ────────────────────────────────────────────────────── */

typedef struct {
	const char *target;
	const char *size;
	const char *fs;
	const char *label;
	const char *type;      /* mktable's gpt|dos */
	const char *start;     /* mkpart's "put it in THIS gap" */
	bool  yes;
	bool  dry;
} popts_t;

static void parse_opts(int argc, char **argv, const char *verb, popts_t *o)
{
	memset(o, 0, sizeof *o);
	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strncmp(a, "--size=", 7))        o->size = a + 7;
		else if (!strncmp(a, "--fs=", 5))     o->fs = a + 5;
		else if (!strncmp(a, "--label=", 8))  o->label = a + 8;
		else if (!strncmp(a, "--type=", 7))   o->type = a + 7;
		else if (!strncmp(a, "--start=", 8))  o->start = a + 8;
		else if (!strcmp(a, "--yes"))         o->yes = true;
		else if (!strcmp(a, "--dry-run") || !strcmp(a, "-n")) o->dry = true;
		else if (a[0] == '-') die("%s: unknown option '%s'", verb, a);
		else if (!o->target)  o->target = a;
		else die("%s: one device at a time (got '%s' as well)", verb, a);
	}
}

/* The sfdisk to run, overridable so the test suite can put a recorder on PATH
 * and assert the argv and the script rather than repartitioning the machine it
 * is running on. */
static const char *sfdisk_tool(void)
{
	const char *env = getenv("SYN_DISKS_SFDISK");
	return (env && *env) ? env : "sfdisk";
}

/* Print it, or run it — and the caller has already built both halves, so the
 * description and the action cannot disagree.
 *
 * Shared with copy.c, which is not a table edit at all but needs exactly this:
 * one command built once, described under --dry-run in the words it will be
 * run in, and gated behind --yes. A second copy of this loop would be a second
 * place for the description and the action to drift apart.
 *
 * Returns the exit status this command should give: 0 done, 1 refused by the
 * tool, 2 described but --yes was missing, 3 the tool is not installed. */
int pt_plan_do(char *const argv[], const char *script, const char *dev,
               const char *what, const char *tool, const char *warn,
               bool yes, bool dry)
{
	char *line = cmd_display(argv);

	if (dry || !yes) {
		if (g_out == OUT_REC) {
			rec_row(2, "field", "value");
			rec_row(2, "command", line);
			if (script && *script)
				rec_row(2, "script", script);
			rec_row(2, "destroys", what);
			/* A WARNING is not a `blocked`, and the difference is which
			 * one leaves the front-end's button live. See the format
			 * dialogue, which has rendered both since the exFAT stick. */
			if (warn && *warn)
				rec_row(2, "warn", warn);
			if (!have_cmd(tool)) {
				char *b = xasprintf("%s is not installed", tool);
				rec_row(2, "blocked", b);
				free(b);
			}
		} else {
			printf("would run: %s\n", line);
			if (script && *script)
				printf("  with: %s", script);
			printf("%s%s%s\n", C_WARN(), what, C_RESET());
			if (warn && *warn)
				printf("%swarning: %s%s\n", C_WARN(), warn, C_RESET());
		}
		free(line);
		return dry ? 0 : 2;
	}
	free(line);

	if (!have_cmd(tool)) {
		fprintf(stderr, "syn-disks: %s is not installed\n", tool);
		return 3;
	}

	int st = 0;
	char *out = run_capture_in(argv, script, &st);
	strip_trailing_newline(out);

	if (g_out == OUT_REC) {
		/* The DEVICE, not argv[0] — which is pkexec, or the sfdisk the test
		 * suite substituted. A record naming the tool instead of the thing it
		 * acted on is one the GUI cannot match to a row. */
		rec_row(3, "device", "status", "detail");
		rec_row(3, dev, st == 0 ? "ok" : "failed", out);
	} else if (st == 0) {
		printf("%s\n", *out ? out : "done");
	} else {
		fprintf(stderr, "%s%s%s\n", C_BAD(),
		        *out ? out : "the tool refused", C_RESET());
	}

	free(out);
	return st == 0 ? 0 : 1;
}

/* The four table edits below, all of which speak to sfdisk and none of which
 * warns about anything the description does not already say. */
static int sfdisk_do(char *const argv[], const char *script, const char *dev,
                     const char *what, bool yes, bool dry)
{
	int rc = pt_plan_do(argv, script, dev, what, sfdisk_tool(), NULL,
	                    yes, dry);
	/* Which package to install is knowledge about sfdisk in particular, so it
	 * belongs here rather than in the shared runner — where it would have to
	 * be right about every tool this program might ever drive. */
	if (rc == 3 && g_out != OUT_REC)
		fprintf(stderr, "  Install util-linux to partition from here.\n");
	return rc;
}

/* The kernel learns about a new partition from udev, which is a moment behind
 * sfdisk returning. Polling sysfs rather than sleeping a fixed time: on an
 * empty stick the node is there almost at once, and on a busy machine a fixed
 * sleep is either wasted or too short. */
static char *wait_for_partition(const char *disk, char **before, size_t nbefore)
{
	for (int tries = 0; tries < 40; tries++) {
		size_t now = 0;
		char **parts = sd_partitions(disk, &now);
		for (size_t i = 0; i < now; i++) {
			bool seen = false;
			for (size_t j = 0; j < nbefore && !seen; j++)
				seen = !strcmp(parts[i], before[j]);
			if (!seen) {
				char *fresh = xstrdup(parts[i]);
				sd_free_list(parts, now);
				return fresh;
			}
		}
		sd_free_list(parts, now);
		nanosleep(&(struct timespec){ .tv_nsec = 100 * 1000 * 1000 }, NULL);
	}
	return NULL;
}

/* The disk a partitioning argument refers to, plus the partition number when
 * the argument was a partition. */
static char *disk_of(const char *arg, const char *verb, char **kname_out,
                     int *number_out)
{
	char *k = sd_kernel_name(arg);
	if (!k)
		die("%s: %s is not a block device", verb, arg);

	if (number_out)
		*number_out = 0;
	char *disk = NULL;

	if (sd_is_partition(k)) {
		char *num = sd_attr(k, "partition");
		if (number_out && num)
			*number_out = (int)strtol(num, NULL, 10);
		free(num);
		disk = sd_parent_disk(k);
	}
	if (!disk)
		disk = xstrdup(k);

	if (kname_out)
		*kname_out = k;
	else
		free(k);
	return disk;
}

/* ── mkpart ─────────────────────────────────────────────────────────────── */

int cmd_mkpart(int argc, char **argv)
{
	popts_t o;
	parse_opts(argc, argv, "mkpart", &o);

	if (!o.target)
		die("mkpart: need a disk (see: syn-disks list)");

	const fs_kind_t *kind = NULL;
	if (o.fs) {
		kind = fs_find(o.fs);
		if (!kind)
			die("mkpart: '%s' is not a filesystem this offers", o.fs);
	}
	if (!fs_label_ok(o.label))
		die("mkpart: a label may hold up to 32 letters, digits, spaces, "
		    "'-', '_' and '.' — '%s' does not", o.label);

	char *disk = disk_of(o.target, "mkpart", NULL, NULL);

	const char *ptt = "";
	size_t n = 0;
	pt_slot_t *slots = pt_layout(disk, &n, &ptt);

	/* GUARD_ADD, and the difference from the other three matters: a new
	 * partition goes into space nothing is using, so nothing that exists is
	 * touched and none of the in-use rules are an objection to it. Asking
	 * with GUARD_MODIFY instead would refuse this on the system drive —
	 * "/" is on the disk, which is true and, for free space at the far end of
	 * it, beside the point. On a laptop with one disk that would be every
	 * partition anybody wanted to make.
	 *
	 * FIRST, and before the table check below, because "this drive cannot be
	 * written at all" outranks "it has no table". The other order sends
	 * somebody who has a write-protect switch set off to run mktable, which
	 * then refuses for the reason they could have been told first. */
	char *ddisk = xasprintf("/dev/%s", disk);
	if (guard_refuse(disk, ddisk, "partition", GUARD_ADD)) {
		free(ddisk);
		pt_free_layout(slots, n);
		free(disk);
		return 1;
	}

	/* A REFUSAL IS AN ANSWER, and under --rec it has to arrive as records with
	 * the way out beside it — the same contract format has kept since the
	 * window spent a day showing a greyed button and no reason.
	 *
	 * This one went to stderr as prose, so the New… dialogue could show the
	 * sentence and nothing else: no `fix` field meant no button, and the only
	 * route out of the dialogue was a command line the person is not in. */
	if (!*ptt) {
		pt_free_layout(slots, n);
		char *why = xasprintf("%s has no partition table", ddisk);
		guard_report_refusal(ddisk, why, "mktable");
		free(why);
		free(ddisk);
		free(disk);
		return 1;
	}
	free(ddisk);

	unsigned long long want = 0;
	if (o.size && !parse_size(o.size, &want))
		die("mkpart: '%s' is not a size — try 20G, 512MiB or a plain number "
		    "of bytes", o.size);

	/* --start names WHICH free space, by any byte offset inside it.
	 *
	 * Without it a front-end that draws the gaps has no way to act on the one
	 * that was clicked: it would show three gaps, let somebody pick the middle
	 * one, and then make the partition in the largest — which is not a
	 * surprise the dry run can even reveal, because the dry run would describe
	 * the same wrong gap the command was about to use.
	 *
	 * An OFFSET rather than an index: a gap's number changes the moment
	 * anything else on the disk does, and a stale index is a request to
	 * partition somewhere nobody looked at. The offset is `table`'s own
	 * `start` field, and if the layout has changed underneath, it either still
	 * names free space or it names none and this refuses. */
	unsigned long long at = 0;
	if (o.start && !parse_size(o.start, &at))
		die("mkpart: '%s' is not an offset — it is a number of bytes, as "
		    "`syn-disks table` prints it", o.start);

	const pt_slot_t *best = NULL;
	for (size_t i = 0; i < n; i++) {
		if (!slots[i].gap)
			continue;
		if (o.start) {
			if (at >= slots[i].start && at < slots[i].start + slots[i].bytes)
				best = &slots[i];
		} else if (!best || slots[i].bytes > best->bytes) {
			/* The LARGEST gap, not the first one that fits. The first fit on
			 * a disk that has been repartitioned a few times is routinely a
			 * 200MB scrap between two old partitions, and "make me a
			 * partition" landing there instead of in the 800GB at the end is a
			 * surprise nobody asked for. */
			best = &slots[i];
		}
	}

	/* Records for these two as well, and for the same reason: a front-end
	 * reading stdout must not have to tell "it refused" apart from "it said
	 * nothing". Neither has a way out this program can offer, so both say so. */
	if (!best && o.start) {
		pt_free_layout(slots, n);
		char *dd = xasprintf("/dev/%s", disk);
		char *why = xasprintf("there is no free space at byte %llu of %s", at,
		                      dd);
		/* Its own code: this is the one refusal here that is not about the
		 * drive at all. The offset came from a `table` that has since been
		 * overtaken, and re-reading is both the way out and something a window
		 * can do by itself. */
		guard_report_refusal(dd, why, "reread");
		free(why);
		free(dd);
		free(disk);
		return 1;
	}
	if (!best) {
		pt_free_layout(slots, n);
		char *dd = xasprintf("/dev/%s", disk);
		char *why = xasprintf("%s has no free space", dd);
		guard_report_refusal(dd, why, "none");
		free(why);
		free(dd);
		free(disk);
		return 1;
	}

	/* Aligned to a megabyte at BOTH ends: the start rounded up so the
	 * partition begins on a boundary, the size rounded down so it still fits
	 * inside the gap afterwards. Rounding the size up would produce a
	 * partition one megabyte past the free space it was placed in, which
	 * sfdisk refuses — after the confirmation. */
	unsigned long long start = (best->start + PT_ALIGN - 1) / PT_ALIGN * PT_ALIGN;
	unsigned long long avail = (best->start + best->bytes > start)
	                         ? best->start + best->bytes - start : 0;
	avail = avail / PT_ALIGN * PT_ALIGN;

	if (avail < PT_ALIGN) {
		pt_free_layout(slots, n);
		fprintf(stderr, "%ssyn-disks: the free space on /dev/%s is too small "
		        "for a partition%s\n", C_BAD(), disk, C_RESET());
		free(disk);
		return 1;
	}

	unsigned long long size = want ? want / PT_ALIGN * PT_ALIGN : avail;
	if (want && size > avail) {
		char *asked = human_size(want);
		char *have = human_size(avail);
		fprintf(stderr, "%ssyn-disks: %s asked for, but %s free space on "
		        "/dev/%s is %s%s\n", C_BAD(), asked,
		        o.start ? "the chosen" : "the largest", disk, have, C_RESET());
		free(asked);
		free(have);
		pt_free_layout(slots, n);
		free(disk);
		return 1;
	}
	if (size < PT_ALIGN)
		size = PT_ALIGN;

	pt_free_layout(slots, n);

	int number = pt_next_number(disk);
	if (number == 0)
		die("mkpart: /dev/%s already has 128 partitions", disk);

	char *dev = xasprintf("/dev/%s", disk);
	/* Sectors, because that is what a script speaks. Converted at this one
	 * place, from the bytes everything above works in. */
	char *script = o.label && *o.label
	    ? xasprintf("start=%llu, size=%llu, name=\"%s\"\n",
	                start / 512ULL, size / 512ULL, o.label)
	    : xasprintf("start=%llu, size=%llu\n", start / 512ULL, size / 512ULL);

	const char *priv = priv_prefix();
	char *cmd[8];
	int c = 0;
	if (priv)
		cmd[c++] = (char *)priv;
	cmd[c++] = (char *)sfdisk_tool();
	cmd[c++] = (char *)"--append";
	cmd[c++] = dev;
	cmd[c] = NULL;

	char *hu = human_size(size);
	char *what = xasprintf("adds a %s partition to %s", hu, dev);

	/* Recorded BEFORE sfdisk runs. The new partition is whichever name is
	 * there afterwards and was not there before — asking sysfs for "the
	 * highest number" would pick the wrong one on a disk where the free slot
	 * being filled is in the middle. */
	size_t nbefore = 0;
	char **before = sd_partitions(disk, &nbefore);

	int rc = sfdisk_do(cmd, script, dev, what, o.yes, o.dry);

	if (rc == 0 && o.yes && !o.dry) {
		char *fresh = wait_for_partition(disk, before, nbefore);
		if (!fresh) {
			/* The partition is on the disk; the kernel has simply not caught
			 * up, and saying "done" without saying that would leave somebody
			 * looking for a node that appears a second later. */
			fprintf(stderr, "%ssyn-disks: partition written, but the kernel "
			        "has not shown it yet — run `partprobe %s`%s\n",
			        C_WARN(), dev, C_RESET());
		} else if (kind) {
			char *pdev = xasprintf("/dev/%s", fresh);
			char *mk[10];
			fs_mkfs_argv(kind, o.label, pdev, mk);
			int st = 0;
			char *out = run_capture(mk, &st, false);
			strip_trailing_newline(out);
			if (st != 0) {
				fprintf(stderr, "%s%s%s\n", C_BAD(),
				        *out ? out : "mkfs refused", C_RESET());
				rc = 1;
			} else if (g_out == OUT_REC) {
				/* No header row: sfdisk_do has already opened a three-column
				 * table and this is a second row in it. A record stream whose
				 * column names appear twice is one the reader re-parses
				 * halfway through. */
				rec_row(3, pdev, "ok", o.fs);
			} else {
				printf("%s is now %s\n", pdev, o.fs);
			}
			free(out);
			free(pdev);
		} else if (g_out != OUT_REC) {
			printf("/dev/%s\n", fresh);
		}
		free(fresh);
	}

	sd_free_list(before, nbefore);
	free(what);
	free(hu);
	free(script);
	free(dev);
	free(disk);
	return rc;
}

/* ── rmpart ─────────────────────────────────────────────────────────────── */

int cmd_rmpart(int argc, char **argv)
{
	popts_t o;
	parse_opts(argc, argv, "rmpart", &o);

	if (!o.target)
		die("rmpart: need a partition (see: syn-disks table <disk>)");

	char *k = NULL;
	int number = 0;
	char *disk = disk_of(o.target, "rmpart", &k, &number);

	if (number == 0) {
		fprintf(stderr, "%ssyn-disks: %s is a whole drive, not a partition%s\n",
		        C_BAD(), o.target, C_RESET());
		fprintf(stderr, "  To erase its table: syn-disks mktable %s "
		        "--type=gpt --yes\n", o.target);
		free(disk);
		free(k);
		return 1;
	}

	char *dev = xasprintf("/dev/%s", k);
	if (guard_refuse(k, dev, "delete", GUARD_DESTROY)) {
		free(dev);
		free(disk);
		free(k);
		return 1;
	}

	char *ddev = xasprintf("/dev/%s", disk);
	char *snum = xasprintf("%d", number);

	const char *priv = priv_prefix();
	char *cmd[8];
	int c = 0;
	if (priv)
		cmd[c++] = (char *)priv;
	cmd[c++] = (char *)sfdisk_tool();
	cmd[c++] = (char *)"--delete";
	cmd[c++] = ddev;
	cmd[c++] = snum;
	cmd[c] = NULL;

	char *hu = human_size(sd_size_bytes(k));
	char *what = xasprintf("destroys %s and the %s on it", dev, hu);

	/* No script: --delete takes the number as an argument and reads nothing. */
	int rc = sfdisk_do(cmd, NULL, dev, what, o.yes, o.dry);

	free(what);
	free(hu);
	free(snum);
	free(ddev);
	free(dev);
	free(disk);
	free(k);
	return rc;
}

/* ── resize ─────────────────────────────────────────────────────────────── */

int cmd_resize(int argc, char **argv)
{
	popts_t o;
	parse_opts(argc, argv, "resize", &o);

	if (!o.target)
		die("resize: need a partition (see: syn-disks table <disk>)");
	if (!o.size)
		die("resize: need --size=SIZE (the new size, which must be larger)");

	char *k = NULL;
	int number = 0;
	char *disk = disk_of(o.target, "resize", &k, &number);

	if (number == 0) {
		fprintf(stderr, "%ssyn-disks: %s is a whole drive, not a partition%s\n",
		        C_BAD(), o.target, C_RESET());
		free(disk);
		free(k);
		return 1;
	}

	unsigned long long want = 0;
	if (!parse_size(o.size, &want))
		die("resize: '%s' is not a size — try 20G, 512MiB or a plain number "
		    "of bytes", o.size);

	char *dev = xasprintf("/dev/%s", k);

	/* GUARD_MODIFY, not GUARD_DESTROY: the partition stays and keeps its
	 * UUID, so an fstab entry naming it still resolves afterwards. Everything
	 * else — mounted, swap, unlocked on top, holding "/" — applies exactly as
	 * it does to a deletion, because growing a partition still rewrites the
	 * table underneath a live filesystem. */
	if (guard_refuse(k, dev, "resize", GUARD_MODIFY)) {
		free(dev);
		free(disk);
		free(k);
		return 1;
	}

	unsigned long long now = sd_size_bytes(k);
	unsigned long long size = want / PT_ALIGN * PT_ALIGN;

	if (size <= now) {
		char *hnow = human_size(now);
		char *hwant = human_size(size);
		fprintf(stderr, "%ssyn-disks: refusing to shrink %s from %s to %s.%s\n",
		        C_BAD(), dev, hnow, hwant, C_RESET());
		fprintf(stderr, "  The filesystem inside would still believe it owns "
		        "the blocks past the new end,\n"
		        "  and would destroy itself the next time it wrote to one. "
		        "Shrink the filesystem\n"
		        "  first, with the tool that belongs to it.\n");
		free(hwant);
		free(hnow);
		free(dev);
		free(disk);
		free(k);
		return 1;
	}

	/* The room to grow into is the free space that FOLLOWS this partition,
	 * and nothing else. Free space before it, or elsewhere on the disk, is
	 * unreachable without moving the partition — which is a copy of every
	 * byte on it, not a table edit. */
	const char *ptt = "";
	size_t n = 0;
	pt_slot_t *slots = pt_layout(disk, &n, &ptt);

	unsigned long long start = 0, ceiling = 0;
	for (size_t i = 0; i < n; i++) {
		if (slots[i].gap || strcmp(slots[i].kname, k))
			continue;
		start = slots[i].start;
		ceiling = slots[i].bytes;
		if (i + 1 < n && slots[i + 1].gap)
			ceiling += slots[i + 1].bytes;
		else if (i + 1 == n)
			ceiling = pt_usable_end(disk, ptt) - start + 1;
		break;
	}
	pt_free_layout(slots, n);

	ceiling = ceiling / PT_ALIGN * PT_ALIGN;
	if (ceiling && size > ceiling) {
		char *hwant = human_size(size);
		char *hmax = human_size(ceiling);
		fprintf(stderr, "%ssyn-disks: %s asked for, but %s can only grow to "
		        "%s — the free space after it ends there%s\n",
		        C_BAD(), hwant, dev, hmax, C_RESET());
		free(hmax);
		free(hwant);
		free(dev);
		free(disk);
		free(k);
		return 1;
	}

	char *ddev = xasprintf("/dev/%s", disk);
	char *snum = xasprintf("%d", number);
	/* The start is repeated rather than left out. `sfdisk -N` merges the
	 * script line over the existing entry and an omitted start keeps the old
	 * one — but "keeps it" is a behaviour to rely on, and stating the value
	 * this program has just read is a fact. */
	char *script = xasprintf("start=%llu, size=%llu\n",
	                         start / 512ULL, size / 512ULL);

	const char *priv = priv_prefix();
	char *cmd[8];
	int c = 0;
	if (priv)
		cmd[c++] = (char *)priv;
	cmd[c++] = (char *)sfdisk_tool();
	cmd[c++] = (char *)"-N";
	cmd[c++] = snum;
	cmd[c++] = ddev;
	cmd[c] = NULL;

	char *hnow = human_size(now);
	char *hnew = human_size(size);
	char *what = xasprintf("grows %s from %s to %s; the filesystem inside "
	                       "still ends where it did", dev, hnow, hnew);

	int rc = sfdisk_do(cmd, script, dev, what, o.yes, o.dry);

	if (rc == 0 && o.yes && !o.dry && g_out != OUT_REC)
		printf("%sthe partition is bigger; the filesystem is not. Grow it "
		       "with resize2fs, btrfs filesystem resize or xfs_growfs.%s\n",
		       C_WARN(), C_RESET());

	free(what);
	free(hnew);
	free(hnow);
	free(script);
	free(snum);
	free(ddev);
	free(dev);
	free(disk);
	free(k);
	return rc;
}

/* ── mktable ────────────────────────────────────────────────────────────── */

int cmd_mktable(int argc, char **argv)
{
	popts_t o;
	parse_opts(argc, argv, "mktable", &o);

	if (!o.target)
		die("mktable: need a disk (see: syn-disks list)");

	const char *type = o.type ? o.type : "gpt";
	if (strcmp(type, "gpt") && strcmp(type, "dos"))
		die("mktable: --type must be gpt or dos, not '%s'", type);

	char *k = sd_kernel_name(o.target);
	if (!k)
		die("mktable: %s is not a block device", o.target);
	if (sd_is_partition(k))
		die("mktable: %s is a partition — a table belongs to a whole drive",
		    o.target);

	char *dev = xasprintf("/dev/%s", k);

	/* GUARD_DESTROY on the DISK, which walks down into every partition on it.
	 * A new table discards all of them at once, so a single mounted
	 * filesystem anywhere on the drive is enough to refuse — and that is why
	 * this is the one partitioning operation the system drive can never
	 * reach. */
	if (guard_refuse(k, dev, "erase the partition table of", GUARD_DESTROY)) {
		free(dev);
		free(k);
		return 1;
	}

	char *script = xasprintf("label: %s\n", type);

	const char *priv = priv_prefix();
	char *cmd[8];
	int c = 0;
	if (priv)
		cmd[c++] = (char *)priv;
	cmd[c++] = (char *)sfdisk_tool();
	cmd[c++] = dev;
	cmd[c] = NULL;

	int count = sd_count_partitions(k);
	char *what = count > 0
	    ? xasprintf("destroys the %d partition%s on %s and everything on them",
	                count, count == 1 ? "" : "s", dev)
	    : xasprintf("writes a new %s table on %s", type, dev);

	int rc = sfdisk_do(cmd, script, dev, what, o.yes, o.dry);

	free(what);
	free(script);
	free(dev);
	free(k);
	return rc;
}
