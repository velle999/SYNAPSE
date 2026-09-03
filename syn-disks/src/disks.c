/* disks.c — the three questions this program exists to answer.
 *
 *   list          what drives are in this machine
 *   parts <disk>  what is on one of them
 *   info <dev>    everything known about one device
 *
 * All three emit the column names as their first record, so a column added
 * here appears in the GUI with no QML change and the two can never disagree
 * about what field three is. Every field is percent-encoded by rec_row.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

/* A drive's name for a human. The model is what is printed on the label of the
 * thing in the machine; "sda" identifies a device, not hardware, and it
 * changes when disks are plugged in a different order. */
static char *disk_title(const char *kname)
{
	/* ⚠ lsblk BEFORE sysfs, which inverts the rule everywhere else in this
	 * program, because here the tool genuinely knows more.
	 *
	 * device/model comes from the SCSI INQUIRY page, whose model field is
	 * sixteen bytes — so the 8TB Seagate in this machine reads
	 * "ST8000VN004-2M21" there and "ST8000VN004-2M2101" from lsblk, and the
	 * Samsung reads "Samsung SSD 840" against "Samsung SSD 840 EVO 250GB".
	 * Both sysfs answers are truncations, and a truncated model number is
	 * the wrong string to hand somebody trying to identify a drive or find
	 * its warranty. udev reads the full ATA IDENTIFY response instead.
	 *
	 * sysfs stays the fallback, so a machine with no lsblk still names its
	 * drives — just more briefly. */
	const lsblk_t *lb = lsblk_for(kname);
	if (lb->model && *lb->model)
		return xstrdup(lb->model);

	char *model = sd_attr(kname, "device/model");
	if (model && *model)
		return model;
	free(model);

	char *dm = sd_attr(kname, "dm/name");
	if (dm && *dm)
		return dm;
	free(dm);

	return xstrdup(kname);
}

static char *disk_vendor(const char *kname)
{
	char *v = sd_attr(kname, "device/vendor");
	/* SCSI pads the vendor field to eight characters and ATA bridges fill it
	 * with the literal string "ATA", which tells a user nothing they cannot
	 * see from the bus column. */
	if (v && (!*v || !strcmp(v, "ATA"))) {
		free(v);
		return NULL;
	}
	return v;
}

/* The mounts of a device as one displayable string. A btrfs with subvolumes
 * has several and showing only the first is how "/" goes missing from a disk
 * that plainly holds the running system. */
static char *mounts_joined(const char *kname)
{
	size_t n = 0;
	char **pts = mt_points_of(kname, &n);
	if (n == 0) {
		sd_free_list(pts, n);
		return xstrdup("");
	}

	char *out = xstrdup(pts[0]);
	for (size_t i = 1; i < n; i++) {
		char *grown = xasprintf("%s, %s", out, pts[i]);
		free(out);
		out = grown;
	}
	sd_free_list(pts, n);
	return out;
}

/* Used and total across every mount of a device, taking the LARGEST total
 * rather than the sum: five subvolumes of one btrfs each report the whole
 * filesystem, and adding them up claims a 234GB disk holds 1.2TB. */
static void usage_of_device(const char *kname, unsigned long long *used,
                            unsigned long long *total)
{
	*used = 0;
	*total = 0;

	size_t n = 0;
	char **pts = mt_points_of(kname, &n);
	for (size_t i = 0; i < n; i++) {
		unsigned long long u = 0, t = 0;
		mt_usage(pts[i], &u, &t);
		if (t > *total) {
			*total = t;
			*used = u;
		}
	}
	sd_free_list(pts, n);
}

/* ── list ───────────────────────────────────────────────────────────────── */

int cmd_list(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		die(_("list: unknown option '%s'"), argv[i]);

	size_t nd = 0;
	char **disks = sd_all_disks(&nd);

	char *rootdev = mt_root_device();
	size_t nrootbase = 0;
	char **rootbase = rootdev ? sd_base_disks(rootdev, &nrootbase) : NULL;

	/* `size` is the human string and `bytes` is the number. Both, because the
	 * command line wants "7.3 TiB" and the GUI needs to draw a partition bar
	 * in proportion — and a front-end parsing "7.3 TiB" back into a number
	 * would be re-deriving something this program already knew exactly. */
	if (g_out == OUT_REC)
		rec_row(10, "device", "name", "kind", "bus", "size", "bytes",
		        "table", "parts", "system", "serial");

	for (size_t i = 0; i < nd; i++) {
		const char *k = disks[i];
		char *dev = xasprintf("/dev/%s", k);
		char *title = disk_title(k);
		char *serial = sd_attr(k, "device/serial");
		char *vendor = disk_vendor(k);
		unsigned long long bytes = sd_size_bytes(k);
		char *size = human_size(bytes);
		const lsblk_t *lb = lsblk_for(k);

		/* Whether the running system lives on this drive. It is the first
		 * thing somebody about to reformat something needs to know, so it
		 * is a column and not a footnote. */
		bool is_system = false;
		for (size_t r = 0; r < nrootbase && !is_system; r++)
			if (!strcmp(rootbase[r], k))
				is_system = true;

		/* Partitions counted from sysfs rather than from lsblk: the kernel
		 * knows how many there are whether or not anything can identify
		 * what is on them. */
		char *pcount = xasprintf("%d", sd_count_partitions(k));

		char *shown = vendor ? xasprintf("%s %s", vendor, title)
		                     : xstrdup(title);

		char *sbytes = xasprintf("%llu", bytes);

		if (g_out == OUT_REC) {
			rec_row(10, dev, shown, sd_kind(k), sd_transport(k), size, sbytes,
			        (lb->pttype && *lb->pttype) ? lb->pttype : "none",
			        pcount, is_system ? "system" : "",
			        serial ? serial : "");
		} else {
			printf("%s%-14s%s %s%-30s%s %s%9s%s  %s%-9s%s %s%s%s\n",
			       C_ACCENT(), dev, C_RESET(),
			       C_BOLD(), shown, C_RESET(),
			       C_RESET(), size, C_RESET(),
			       C_DIM(), sd_kind(k), C_RESET(),
			       is_system ? C_WARN() : C_DIM(),
			       /* The record path a few lines up writes the bare token
			        * "system"; this is the sentence a person reads, so it is
			        * the one that translates. */
			       is_system ? _("system disk") : "", C_RESET());
		}

		free(sbytes);
		free(pcount);
		free(shown);
		free(vendor);
		free(serial);
		free(size);
		free(title);
		free(dev);
	}

	sd_free_list(rootbase, nrootbase);
	free(rootdev);
	sd_free_list(disks, nd);
	lsblk_done();

	return nd ? 0 : 100;
}

/* ── parts ──────────────────────────────────────────────────────────────── */

/* One row of the partition table, plus anything unlocked or assembled on top
 * of it. A LUKS partition whose contents are open shows both: the container
 * with its type, and the volume with its filesystem and mounts. Showing only
 * the container is why an encrypted disk looks empty. */
static void emit_volume(const char *k, int depth)
{
	char *dev = xasprintf("/dev/%s", k);
	const lsblk_t *lb = lsblk_for(k);
	unsigned long long bytes = sd_size_bytes(k);
	char *size = human_size(bytes);
	char *mounts = mounts_joined(k);

	unsigned long long used = 0, total = 0;
	usage_of_device(k, &used, &total);

	char *label = NULL;
	if (lb->label && *lb->label)
		label = xstrdup(lb->label);
	else if (lb->partlabel && *lb->partlabel)
		label = xstrdup(lb->partlabel);
	else {
		char *dm = sd_attr(k, "dm/name");
		label = (dm && *dm) ? dm : (free(dm), xstrdup(""));
	}

	char *sdepth = xasprintf("%d", depth);
	char *sused = xasprintf("%llu", used);
	char *stotal = xasprintf("%llu", total);
	char *sbytes = xasprintf("%llu", bytes);

	/* Where the partition begins, in bytes, so a front-end can draw the gaps
	 * between them. `start` is in 512-byte sectors like `size`, and is absent
	 * on anything that is not a partition — a nested volume has no offset
	 * within the disk of its own. */
	char *rawstart = sd_attr(k, "start");
	unsigned long long startb = rawstart ? strtoull(rawstart, NULL, 10) * 512ULL : 0;
	char *sstart = xasprintf("%llu", startb);
	free(rawstart);

	if (g_out == OUT_REC) {
		rec_row(12, dev, label,
		        (lb->fstype && *lb->fstype) ? lb->fstype : "",
		        size, mounts,
		        (lb->parttype && *lb->parttype) ? lb->parttype : "",
		        (lb->uuid && *lb->uuid) ? lb->uuid : "",
		        sdepth, sused, stotal, sbytes, sstart);
	} else {
		printf("%*s%s%-16s%s %s%-16s%s %s%-11s%s %s%9s%s  %s%s%s\n",
		       depth * 2, "",
		       C_ACCENT(), dev, C_RESET(),
		       C_RESET(), *label ? label : "-", C_RESET(),
		       C_DIM(), (lb->fstype && *lb->fstype) ? lb->fstype : "-", C_RESET(),
		       C_RESET(), size, C_RESET(),
		       C_DIM(), *mounts ? mounts : "not mounted", C_RESET());
	}

	free(sstart);
	free(sbytes);
	free(stotal);
	free(sused);
	free(sdepth);
	free(label);
	free(mounts);
	free(size);
	free(dev);

	/* What is built on this one — the unlocked LUKS volume, the LVM member.
	 * Bounded by construction: a holder is always further from the hardware
	 * than the thing it holds, so this cannot revisit a device. */
	if (depth < 4) {
		size_t nh = 0;
		char **holders = sd_holders(k, &nh);
		for (size_t i = 0; i < nh; i++)
			emit_volume(holders[i], depth + 1);
		sd_free_list(holders, nh);
	}
}

int cmd_parts(int argc, char **argv)
{
	if (argc < 1)
		die(_("parts: need a disk (see: syn-disks list)"));

	char *k = sd_kernel_name(argv[0]);
	if (!k)
		die(_("%s: not a block device"), argv[0]);

	if (sd_is_partition(k)) {
		/* Asking for the partitions OF a partition is nearly always a
		 * mistyped disk name, and answering "none" would look like an
		 * empty disk rather than a wrong question. */
		char *parent = sd_parent_disk(k);
		/* Two whole sentences rather than one with " of /dev/" glued in:
		 * a fragment that small carries no grammar, and the languages that
		 * need a case ending on the disk name cannot put one on a piece
		 * they never see. */
		if (parent)
			die(_("%s is a partition of /dev/%s"), argv[0], parent);
		die(_("%s is a partition"), argv[0]);
	}

	if (g_out == OUT_REC)
		rec_row(12, "device", "label", "fstype", "size", "mounts",
		        "parttype", "uuid", "depth", "used", "total", "bytes", "start");

	int n = 0;
	size_t np = 0;
	char **parts = sd_partitions(k, &np);
	for (size_t i = 0; i < np; i++) {
		emit_volume(parts[i], 0);
		n++;
	}
	sd_free_list(parts, np);

	/* A disk with a filesystem written straight to it — a USB stick written
	 * from an ISO, a floppy-style format. It has no partition table at all,
	 * and reporting "no partitions" would be technically true and useless. */
	if (np == 0) {
		const lsblk_t *lb = lsblk_for(k);
		if (lb->fstype && *lb->fstype) {
			emit_volume(k, 0);
			n++;
		} else if (g_out == OUT_HUMAN) {
			printf(_("%sno partition table%s\n"), C_DIM(), C_RESET());
		}
	}

	free(k);
	lsblk_done();
	return n ? 0 : 100;
}

/* ── info ───────────────────────────────────────────────────────────────── */

/*
 * One `field <TAB> value` row, or one aligned line for a person.
 *
 * ⛔ THE KEY IS TRANSLATED AT THE DRAW SITE AND NOWHERE ELSE. It is the RECORD's
 * field name — `syn-disks --rec info sdz1` is what a script greps for `uuid` or
 * `mounted at` — so the row has to carry the English word. The call sites mark
 * their keys with N_(), which puts them in the catalog and returns them
 * unchanged; the human branch below looks them up.
 *
 * ⚠ NOT THE VALUE. A value here is a model name, a UUID, a size, a mount point
 * or a device path — data, every one of them.
 */
static void info_row(const char *key, const char *val)
{
	if (!val || !*val)
		return;
	if (g_out == OUT_REC)
		rec_row(2, key, val);
	else
		printf("  %s%-18s%s %s\n", C_DIM(), _(key), C_RESET(), val);
}

int cmd_info(int argc, char **argv)
{
	if (argc < 1)
		die(_("info: need a device"));

	char *k = sd_kernel_name(argv[0]);
	if (!k)
		die(_("%s: not a block device"), argv[0]);

	if (g_out == OUT_REC)
		rec_row(2, "field", "value");

	char *dev = xasprintf("/dev/%s", k);
	info_row(N_("device"), dev);
	info_row(N_("kernel name"), k);

	bool part = sd_is_partition(k);
	info_row(N_("type"), part ? "partition" : "disk");

	/* Identity belongs to the DRIVE. A partition has no model, no serial and
	 * no firmware of its own, and disk_title() falling back to the kernel
	 * name meant `info /dev/nvme1n1p2` printed "model: nvme1n1p2" — a row
	 * that looks like an answer and is only the question repeated. */
	char *title = NULL, *vendor = NULL, *serial = NULL;
	if (!part) {
		title = disk_title(k);
		info_row(N_("model"), title);
		vendor = disk_vendor(k);
		info_row(N_("vendor"), vendor);
		serial = sd_attr(k, "device/serial");
		info_row(N_("serial"), serial);
	}
	char *fw = sd_attr(k, "device/rev");
	if (!fw || !*fw) {
		free(fw);
		fw = sd_attr(k, "device/firmware_rev");
	}
	info_row(N_("firmware"), fw);

	unsigned long long bytes = sd_size_bytes(k);
	char *size = human_size(bytes);
	/* ⛔ NOT MARKED: this is the VALUE half of a record row, and a script
	 * reads it. `bytes` here is a unit, the way KiB and GiB are — the
	 * FIELD NAME beside it is what info_row() translates. */
	char *exact = xasprintf("%llu bytes", bytes);
	info_row(N_("size"), size);
	info_row(N_("size (exact)"), exact);

	info_row(N_("kind"), sd_kind(k));
	info_row(N_("bus"), sd_transport(k));

	char *ro = sd_attr(k, "ro");
	if (ro)
		info_row("read-only", !strcmp(ro, "1") ? "yes" : "no");

	if (part) {
		char *parent = sd_parent_disk(k);
		if (parent) {
			char *pdev = xasprintf("/dev/%s", parent);
			info_row(N_("part of"), pdev);
			free(pdev);
			free(parent);
		}
		char *start = sd_attr(k, "start");
		if (start) {
			/* ⛔ NOT MARKED — a record value; see the note by `exact`. */
			char *at = xasprintf("sector %s", start);
			info_row(N_("starts at"), at);
			free(at);
			free(start);
		}
	} else {
		char *lbs = sd_attr(k, "queue/logical_block_size");
		char *pbs = sd_attr(k, "queue/physical_block_size");
		info_row(N_("logical block"), lbs);
		info_row(N_("physical block"), pbs);
		free(lbs);
		free(pbs);
	}

	const lsblk_t *lb = lsblk_for(k);
	info_row(N_("filesystem"), lb->fstype);
	info_row(N_("label"), lb->label);
	info_row(N_("uuid"), lb->uuid);
	info_row(N_("partition type"), lb->parttype);
	info_row(N_("partition label"), lb->partlabel);
	info_row(N_("partition table"), lb->pttype);

	char *mounts = mounts_joined(k);
	info_row(N_("mounted at"), *mounts ? mounts : "not mounted");

	unsigned long long used = 0, total = 0;
	usage_of_device(k, &used, &total);
	if (total) {
		char *hu = human_size(used);
		char *ht = human_size(total);
		char *line = xasprintf(_("%s of %s (%llu%% full)"), hu, ht,
		                       used * 100 / total);
		info_row(N_("usage"), line);
		free(line);
		free(ht);
		free(hu);
	}

	/* Which physical hardware this ultimately sits on, and whether the
	 * running system is on the same. Both are facts nothing else prints, and
	 * both are what somebody about to erase something needs. */
	size_t nb = 0;
	char **base = sd_base_disks(k, &nb);
	if (nb && !(nb == 1 && !strcmp(base[0], k))) {
		char *joined = xasprintf("/dev/%s", base[0]);
		for (size_t i = 1; i < nb; i++) {
			char *g = xasprintf("%s, /dev/%s", joined, base[i]);
			free(joined);
			joined = g;
		}
		info_row(N_("physical disk"), joined);
		free(joined);
	}

	char *rootdev = mt_root_device();
	if (rootdev) {
		size_t nr = 0;
		char **rb = sd_base_disks(rootdev, &nr);
		bool shared = false;
		for (size_t i = 0; i < nb && !shared; i++)
			for (size_t j = 0; j < nr && !shared; j++)
				if (!strcmp(base[i], rb[j]))
					shared = true;
		info_row(N_("holds this system"), shared ? "yes" : "no");
		sd_free_list(rb, nr);
		free(rootdev);
	}
	sd_free_list(base, nb);

	free(mounts);
	free(ro);
	free(exact);
	free(size);
	free(fw);
	free(serial);
	free(vendor);
	free(title);
	free(dev);
	free(k);
	lsblk_done();
	return 0;
}
